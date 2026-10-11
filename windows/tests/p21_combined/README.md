# Same-host provider and recorder proof

`run.py <new result folder> --toolkit <toolkit repo>` joins the previously
separate P21k real DirectInput provider and P21j real recorder paths. The host
now accepts a recorder type; its default remains the existing fake recorder,
so the previous lifecycle suite still exercises the same shutdown implementation.

The combined fixture supplies the reviewed Controller/Producer/session Writer.
Actual Core ticks over the real DInputProvider and fake COM generate 16 frames
from two devices, including held buttons and Poll/GetDeviceState pending cases.
There is no call to the old fake-Provider capture generator in this workload.

The exact existing finalizer validates and seals the capture only after normal
exit. Its export must equal a separately retained producer snapshot for every
channel, then the recorded actions run through the real OutRun adapter again.
Normal shutdown and a release that returns within its budget both seal. A stuck
writer, stuck provider reacquisition, ordinary forced exit and stuck Unacquire
must never seal. Fake COM counters independently require no force, release
during a call, or use after release. A changed channel must fail exact replay.

This remains an offline fixture. It does not demonstrate real hardware, game
host adoption, portable replay, WM_ENDSESSION or force feel. Synchronous report
I/O retains the existing outer process-timeout limitation. The fixture reuses
the original P21j supervisor and codec with an explicit entry-point guard;
no `child` or `wmain` macro can change identifiers in SDK headers.

The reviewed native writer's content pin accepts LF or CRLF checkout bytes.
Only CRLF-to-LF normalization is allowed; a BOM, lone CR or any content change
refuses. The original byte hash is still pinned before and after the run.
`writer-pin.json` retains both hashes; five changed-content controls accompany
the two accepted checkout representations.
