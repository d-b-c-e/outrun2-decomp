# Desktop host component

`ProviderHost<Recorder>` owns the real DirectInput provider/Core lifetime.
It is extracted from the independently reviewed P21k implementation; the
existing P21k and combined real-recorder fixtures now call this same code.
There are no dependencies on fixture headers, fake COM, test workers or gates.
`host_lifecycle.hpp` supplies the shared outcome/report and bounded thread join.

Production defaults to the Core's 100 ms freshness bound. The compatibility
wrapper in `tests/p21_provider_host` explicitly preserves its former 10-second
offline default; the real-device probe already explicitly selects 100 ms.
The recorder is supplied by the caller and must not start a worker in its
constructor. A no-capture slot may report that it has nothing to drain, which
does not represent a complete recording or authorize a session seal.

This extraction is source groundwork for desktop adoption, not a wired game
host. No desktop build, installed game, controls path or recorder has changed.
The caller must still implement the documented neutral step, fence/admission,
control worker, capture lifecycle, immutable test latch and unresolved exit.
Synchronous report file I/O still needs the identity-bound external deadline.
Host-side detach/termination policy belongs here, never in provider/Core workers.

Regression runners retain the new host files in their source pins and mutate
the implementation itself. Passing a copied implementation is insufficient.
