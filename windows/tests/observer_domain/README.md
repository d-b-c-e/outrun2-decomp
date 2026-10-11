# Typed observer domain (source only)

Run `py -3 -B run.py <new result directory>`. This exercises the actual Core
resolver and event ring on a fake provider, then the same domain encoder and
reader that a desktop adapter can use. No game, devices, injection or force.

Observer v2 is an explicit sequence of 32-bit words, not a memcpy of C++
objects. Integers keep all 64 bits; float bit patterns, raw button bytes,
device/epoch/read sequences and injection marks survive. The payload contains
process creation identity, run nonce, executable/build/profile hashes, the exact
profile device-table size (sample and event indices must be inside it), the
Core frame and up to eight samples, sixteen adapter signals and 64 events.
Oversize data is refused with prior output untouched; no silent truncation.
The build hash must identify the adapter's signal schema and units as well as
the implementation. A source signal has its own frame/time/offset; this
distinguishes previous-tick game consumption from current-tick commands.

The reader checks the exact expected identity, a caller-supplied monotonic
clock and freshness limit, forward frame/event sequences, canonical length,
zero tail, enum/range validity and source links. Repeated or stale snapshots
never replace the last accepted output. Its result reports overwritten events,
including events lost before the first read. Identity is a join against known
evidence, not security authentication against a malicious local process.
Both producer and reader MUST use the system-wide QPC clock in microseconds,
with the same integer conversion as `DInputProvider::now_us()` (whole seconds
times 1,000,000 plus the fractional remainder). Passing a process-relative
Stopwatch/steady-clock origin is invalid. Process creation FILETIME is identity
only, never substituted for QPC time.

Event details remain in the full batch/status log; the ring carries sequence,
time, kind, device, epoch and batch. Status and event-drop counters have explicit
fields and required `compose` arguments from Core. Force/fence flags are
observations supplied by that host, not authority to enable an output.

This completes the domain portion of P19 offline only. Desktop mapping startup,
clock-origin agreement, signal schema manifest, full log/recording join and
input-thread budget measurement still belong to integration. A desktop host
must reject profiles exceeding the eight-sample observation bound before
starting a qualification run. This does not change the Core's device limit.
