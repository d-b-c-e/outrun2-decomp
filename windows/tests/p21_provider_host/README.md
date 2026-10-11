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
  - leases and release: `DInputProvider::shutdown(200 ms)` on a release thread the host owns, waited for at most 200 + 200 ms (Astra 13277). A device still inside a call is leaked to it, never released under it. A release (`Unacquire`, `Release`) still inside the driver is left running with the provider it holds, and the stop goes on;
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
| `unacquire-within` | the shutdown's `Unacquire` of the idle device sleeping 100 ms, inside the 200 ms release budget | waited for, released: resolved, exit 0, **seals** |
| `unacquire-beyond` | the same for 1500 ms, past it (Astra's 1200 ms reproduction, wider) | stop does not wait: "DirectInput release did not return", parked, forced |
| `unacquire-never` | `Unacquire` waiting on an event nobody sets | the same; never seals |
| `ordinary-unacquire-never` | the same, in an ordinary process | ends itself, labelled forced (`0xDBCE0021`) |
| `input-release-never` | the release of DirectInput waiting on an event | "DirectInput release did not return": parked, forced |

For every mode, the fake's own counters, written before the D4 exit, must show no release during a call, no use after release and no force call. Every mode's stop must return within the summed budgets (300 + 200 + 200 + 200 ms, plus 250 ms for scheduling and the report): about 0.4 s measured for every unresolved mode. The seal rule (D8) holds only for `resolved`, `slow-within` and `unacquire-within`.

The fake's additions are opt-in and off by default. zero's `test_dinput_provider` is rebuilt and run against it.

## P21g: the teardown audit

In the code a shim worker can reach (`windows/shim`), the audit forbids:
- R1: the console and FILE streams;
- R2: any static, thread_local or namespace-scope object with a dynamic destructor;
- R3: detaching a thread;
- R4: ending the process or a thread.

It has two halves, each with an exact allowlist where stale entries fail. Both are clean today.

**The lexer**, `windows/tests/teardown_audit.py`, is a sibling of `write_audit.py`. It reads every file, headers included, with comments and literals blanked and namespace scope tracked by braces at any indentation. Unbalanced braces fail closed. `--selftest` checks 50 cases. `--mutations` plants 7 violations in a scratch copy of the real files, among them an indented-namespace global and `std::fopen`, plus a stale allowlist entry, and requires each to be caught.

The lexer cannot know which user types have destructors, or see a call through a macro, an alias or a function pointer (Astra 13114). So:

**The compiler**, `windows/tests/teardown_objects.py`, has clang-cl compile every shim translation unit, and every header on its own, with `-Werror=exit-time-destructors`. It then reads each object's undefined symbols:
- R1: console and FILE-stream entry points, the `vf*printf` family, `__acrt_iob_func`, the iostreams, filebuf;
- R2: `atexit`, `_onexit`, `at_quick_exit`, `__tlregdtor`;
- R3: `_Thrd_detach`;
- R4: the ending calls.

String formatting into a buffer (`snprintf`, `sscanf`) touches no stream and stays allowed. The provider uses both.

`--mutations` plants 12 violations, each caught:
- a custom `OwnedDevice` in an indented namespace;
- an inline namespace and a thread_local;
- a function-local static;
- an explicit `atexit`;
- `std::fopen`, `&std::fwrite` with no call, and `fputs` through a macro;
- `std::cout`;
- detach, `std::exit` and `TerminateProcess`.

A plant of allowed patterns must stay clean: atomics, a mutex, `snprintf`, `sscanf` and a joined thread.

Out of scope, stated rather than claimed:
- fail-fast paths the compiler or library inserts (`std::terminate` on an escaping exception, `_invoke_watson`, `_purecall`);
- code that no shim translation unit compiles or includes.

The P20 recorder worker joins the scope when it is promoted from `tests/` into `windows/shim`.

## Not shown

- Host adoption in `OutRunDesktop`.
- Real COM or devices (slice 4).
- `WM_ENDSESSION`.
- The recorder path through the real writer (P21j covers it).
- Whether a real driver's `Unacquire` or `Release` stalls: the bound holds whatever they do, but only fakes are shown.
