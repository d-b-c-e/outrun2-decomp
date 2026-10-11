# Control-worker handoff (offline)

Run `py -3 -B run.py <new result directory>` from a clean committed checkout.
The proof drives the production Core and Pump with a scripted file source and
fake devices. It includes an actually blocked worker while the input thread
continues ticking, closes admission, then allows the late read to return.

The session is read once, joined to the entry fence's nonce and timed using the
wall clock **after** the read. Batches wait for Core's published admission state;
they never inspect the input thread's mutable `run()`. Identical polled bytes
are counted in the journal and cannot extend TTL. Core still checks changed
bytes, batch order, the full hash, objects and queue capacity. File or journal
errors terminate the test on the next input tick; failed/closed pumps never
retry into an apparently healthy run. Ordinary play never reads injection files.

`close_control()` is a mutex-protected stop gate independent of the worker's
progress. A late session/batch cannot enqueue after closure, including a batch
which was parsing during close. Closure does not itself write a native game
input: the desktop host still owes its explicit final neutral adapter frame.
Core remains terminal and neutral if input ticks continue after closure.

The control thread exclusively owns Pump, including its status accessors.
The caller must keep its Core, provider and Io alive until the worker really
ends. The lifetime host's bounded OS-thread join and unresolved-exit policy
remain required. This slice deliberately supplies no thread or file backend:
Win32 bounded snapshot reads, identity-bearing journal serialization, thread
ownership, final journal drain, capture qualification and desktop wiring remain
integration work. A blocked write cannot stall the input thread, but can lose
log evidence; a qualified capture must refuse a failed/unjoined control worker
or any unexplained journal gap. No live game or installed input path changes.

Follow-up host integration supplies an explicit `finish`: after the input thread
has closed admission and then emitted its terminal neutral tick, the worker drains
the remaining terminal status/event and closes its sink. Read/parse failures
still drain into a healthy journal, while the outcome remains Failed. A broken
journal is never retried, and any reader must require the terminal reason before
qualifying evidence. Diagnostics truncate at a UTF-8 code-point boundary.
`ProviderHost` catches the control thread's exceptions, including API misuse.
Ordinary-play status logging is intentional (INPUT-SHIM section 12); the future
persistent sink still needs bounded/coalesced writes and an explicit policy.
