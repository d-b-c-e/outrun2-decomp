# P21k: the P21 host over the real provider (offline)

This is P28's P21 half (`windows/docs/INPUT-SHIM.md` section 17: "P1-P6 and P21 rerun through the real provider code over fake COM objects"). It was proposed in hcom 12222 and accepted by Astra in 12465.

It is a test composition, not production code and not host adoption: no device, game, installation or launch.

```powershell
py -3 -I -B windows/tests/p21_provider_host/run.py <new result folder> --mutations
```

## What runs

`host.hpp` (`p21k::ProviderHost`) is the reviewed P21 lifetime around zero's real `Core` and `DInputProvider` (slice 3, unchanged), over the fake COM objects of `shim_fake_com.hpp`:
- **The provider worker** runs on its own thread and calls `Core::service_devices`, as `shim.hpp` section 4 has it. Its state (the core and the provider) is shared, so a worker left inside a driver call keeps alive everything it can still call.
- **The input thread** ticks the core and owns `stop()`. The phases:
  - recorder (the reviewed spike's fake writer);
  - leases: `DInputProvider::shutdown(200 ms)`. A device still inside a call is leaked to it, never released under it;
  - workers: one 200 ms join;
  - DirectInput: released only when no call can still use it.
- **D4 as reviewed:** a latched process parks for the identity-bound supervisor; an ordinary one ends itself with `0xDBCE0021`. D9: the destructor never stops or waits.

`main.cpp` runs one supervised child per mode. The parent's waits are all bounded and every call is checked. The modes:

| Mode | The call at stop | Expected |
|---|---|---|
| `resolved` | none | resolved; the device and DirectInput released; normal CRT teardown, exit 0; **seals** |
| `slow-within` | reacquisition `Acquire` sleeping 120 ms, inside the 200 ms lease wait | waited for, then released: resolved, exit 0, **seals** (deterministic near-timeout recovery) |
| `slow-beyond` | the same call for 600 ms, past every budget | held, DirectInput kept, worker unjoined: unresolved, parked, forced by the supervisor; a later return cannot resolve it |
| `never` | `Acquire` waiting on an event nobody sets | held, never released under it: unresolved, parked, forced; never seals |
| `ordinary-never` | the same, in an ordinary process | ends itself, labelled forced (`0xDBCE0021`) |
| `listing-never` | the first `EnumDevices` waiting on an event | DirectInput kept for the listing, worker unjoined: parked, forced |
| `never-off-thread` | as `never`, with `stop()` on another thread | "stop off the input thread", recorder `Cancelled`: parked, forced |

For every mode, the fake's own counters, written before the D4 exit, must show no release during a call, no use after release and no force call. The seal rule (D8) holds only for `resolved` and `slow-within`.

The fake's additions are opt-in and off by default. zero's `test_dinput_provider` is rebuilt and run against it.

## P21g: the teardown audit

`windows/tests/teardown_audit.py` is a sibling of `write_audit.py`, with the same exact-line allowlist, where stale entries fail. In the code a shim worker can reach (`windows/shim`) it forbids:
- R1: stdio;
- R2: any static, thread_local or namespace-scope object with a dynamic destructor;
- R3: detaching a thread;
- R4: ending the process or a thread.

Today it is clean (0 candidate lines). `--selftest` checks 28 cases. `--mutations` plants one violation per rule in a scratch copy of the real files, plus a stale allowlist entry, and requires each to be caught. The P20 recorder worker joins the scope when it is promoted from `tests/` into `windows/shim`.

## Not shown

- Host adoption in `OutRunDesktop`.
- Real COM or devices (slice 4).
- `WM_ENDSESSION`.
- The recorder path through the real writer (P21j covers it).
