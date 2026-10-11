# Owned control worker (offline)

`py -3 -B run.py <fresh result directory>` builds the production ProviderHost,
Core, control Pump and DirectInput provider against fake COM and scripted I/O.
The supervisor retains its child process handle, creation identity and report.
Only its own parked children are ended after a deadline. No game/device/install.

The host starts one control thread, with shared ownership of the Core, its
referenced provider and I/O sink. On stop it closes admission, requests control
stop, waits for final journal drain and sink close, and checks the OS thread's
actual termination against one deadline. Completion and joining are separate.
Read, flush and thread-exit stalls return an unresolved report without destroying
objects still used by the worker. The existing host then parks for the supervisor.
A late read/close never upgrades a cached timeout to completion.

The input caller closes admission, emits/applies its final neutral adapter
frame, stops input ticks, then stops the host. `stop` closes admission again as
defence in depth. The control worker drains the final input events and calls
`Io::finish` on its own thread even after a previous I/O failure. No thread is
detached as successful completion; only the unresolved host destructor has its
documented non-waiting policy, covered by exact teardown audit entries.

Eight child cases cover clean completion, failed append, failed sink finish,
blocked read, blocked finish, a stuck FLS thread-exit callback and a read released
after the deadline, and shutdown without a tick after closing admission. The
host reports the latter unresolved even when its journal and OS thread finish.
The clean case requires normal exit 0, a consumed press and
neutral release, its full final batch journal, and no force or use-after-release.
All seven fault cases refuse resolution and require externally observed shutdown.
Four planted defects must fail: treating a returned function as a joined thread,
ignoring failed journal completion, skipping final journal drain, or ignoring
the missing closure tick. The healthy journal must contain the closure event.

The no-capture test recorder acknowledges it has nothing to drain; this is not
a sealed recording. The journal is a scripted memory sink. Persistent log
serialization, capture binding and desktop adapter startup remain integration
work; a resolved lifetime alone never qualifies a gameplay test or recording.
