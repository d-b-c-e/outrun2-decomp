# Control journal sink (offline)

`py -3 -B run.py <new result directory>` builds the production `JournalFile` / `FileIo`
(`windows/host/control_journal.hpp`) with Core, Pump and the scripted device provider. It writes every case
into fresh disposable folders, reads each journal back with `read_journal.py`, and compares the result with
the case's expectation. No game, device, install or force.

Cases:
- **Round trip:** header, device and start-line rows, status, event, counters and end.
- **Creation:** an ordinary journal creates its own new per-process folder. An existing folder, or an
  existing journal file, is refused and never touched. The constructor creates nothing.
- **Escapes and bounds:** quote, backslash, controls, DEL and non-BMP round trip. A 260-unit control-character
  device name and 512-byte control-character reasons sit exactly at their escaped bounds. One more byte, or
  invalid UTF-8, is refused, never truncated.
- **Full queue:** 1024 events and 256 statuses at maximum size, written in 64 KiB chunks.
- **Short write:** a mid-line short write latches failure. There's no retry and no success end, and the
  reader reports it torn and incomplete.
- **Cap:** the reserved tail reports the cap within the cap. Unwritten lines return their seq numbers.
- **Empty drains:** skipped, counted, and the count is not part of the change test.
- **Off-worker calls:** refused.
- **Unfinished sink:** reported incomplete.
- **Sharing reader:** a concurrent reader sees whole lines only.
- **Atomic drain:** a deterministic demonstration that a split drain breaks the epoch rule while one cut keeps
  it, plus an input thread racing a draining worker over more than 100 epochs.
- **Pump and FileIo:** a latched run over real `inject.on` / `inject.txt` files, closed in the host order
  (close, final tick, finish). It must have exactly one terminal RunChanged event.
- **Reader:** refuses a drifted identity and a seq gap.

This is a minimal slice (Astra 16337/16347): no mutant harness. The deferred items are listed in
`windows/docs/CONTROL-JOURNAL.md` section 10.
