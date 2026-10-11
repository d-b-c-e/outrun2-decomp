# Atomic observer transport proof

`py -3 -B run.py <new result folder>` builds the Windows x64 transport and
tests the actual implementation with no game, input device or force calls.
It rejects an unpublished or odd sequence without changing the prior snapshot,
refuses a wrong-thread or competing writer, forces publication halfway through
a reader copy, and runs three overlapping readers against 2,000 generations.
A separate read-only mapped child is killed halfway through a read; the writer
must continue without waiting and subsequent readers still work.
The producer is paced every ten generations; at least 1,000 coherent reads and
50 distinct generations per reader are required. A separate initialization event refuses readers before atomic
construction is published; zero-filled mapping existence is not readiness.

The runner pins clean source and plants three code defects: accepting an odd
sequence, omitting the final sequence check, and allowing the wrong thread to
publish. Each must fail its specific assertion. Both original and mutated
programs have an external deadline; child termination uses its owned handle.

This is the transport part of P19, not the whole Observer v2 or game adoption.
Domain encoding, version/identity joins, frame freshness and the 64-event ring
still need their own implementation/tests. Coherence alone is not liveness.
The mapping lives while its handles/views remain; the game host must retain its
writer mapping until the input thread stops. The private proof name uses
`Local\DbceShimObserver-...`; the final product/nonce name is not published yet.

Proposed refinement to INPUT-SHIM's deferred platform design: use a lock-free
64-bit sequence (x64 only) and sequentially consistent atomic payload words.
The writer refuses counter exhaustion instead of wrapping. This removes the
32-bit sequence's ABA possibility during a suspended reader. The payload stays
4-byte words with compile-time size/alignment checks. The Windows read-only
mapping test verifies the emitted x64 loads do not write to the view. No claim
is made for another compiler/architecture or a portable C++ interprocess ABI.
Independent peer review is required before connecting it to the game.
