# October 24 road-service private candidate

Source `6dd3a55e4286b486eef2c06dbf36cbb4d9179cef` adds a pending
road-table read to the game's ordinary file-service step. Tweaks fast-load
Mode 3 remains unavailable; other file routes and the frame-deadline pump
are not implemented.

The ClangCL Release desktop and focused `road_file_service` CTest passed.
The test checks pending submission, one successful read, idle, reset
cancellation, and a failed read. The existing package-tool suite passed 8/8.
An optional read-only run against `E:/Games/OutRunDecompSmoke` then passed
15 checks, including one actual retail `.sz` road file copied by the
game-thread service and released by the loader. This does not launch the game.

Private package:
`E:/outrun2-research/private-candidate-6dd3a55-20261024/candidate.zip`
(SHA-256 `51B6FB50C518CCD23B4B96700E3DD7F4F1870944A13146F597F6552C10663A17`).
The 14-member package binds the source commit to an operator-supplied
`OutRunDesktop.exe` (SHA-256
`478C78A8D9F9472C15159EA09D895B946823B4FED06BAD316214EE90A9742A65`).
Build logs are beside the earlier `e4e0ed5` package in
`E:/outrun2-research/private-candidate-e4e0ed5-20261024/road-build2.log`.

The earlier `e4e0ed5` private candidate and installed owner version remain
unchanged.

A fresh private data copy is staged at
`E:/outrun2-research/private-road-smoke-6dd3a55-20261024/game` with the
candidate executable, a minimal windowed configuration, triple screens off,
telemetry off and force output off. The 26 copied retail items include a
private SaveGame; the installed source was read only. The preparation record
is beside the copy.

## Private race check

The original `6dd3a55` executable reached mode 16 in the isolated copy and
closed normally (`run-01`, 55 seconds after the window appeared). The stock
result log did not expose road-service counts, so it could not establish use
of the new queue. A second trace (`run-02`) likewise lacked the counters in
this non-test build. Both runs used a 1280x720 window, `DBCE_FFB_MUTE=1`,
force output off, telemetry off and triple screens off.

Source successor `27c07715f31c555e338b25cbd099b990eb8ddf75` adds the
existing race-area report to ordinary shutdown. Its Release desktop build
passed; the private ZIP is
`E:/outrun2-research/private-candidate-27c0771-20261024/candidate.zip`
(SHA-256 `1B1E64159BD4A49AC0C3F0874C7894B2CCC42B6A243501C05ABC8CF97F40369B`,
EXE SHA-256 `07673CE8B6607923CF38E9F3AF6C334E88A44E0B81CCA097972756CB2507474D`).
The previous private EXE was backed up inside the copy before replacement.

The targeted `run-03` reached mode 16, recorded **8 road-table requests and
29,948 loaded bytes**, reported no race-area first fault, and closed normally
without forced termination. The route's race profile was 902 frames at
60.0 FPS; it is not a performance comparison. Evidence is under
`E:/outrun2-research/private-road-smoke-6dd3a55-20261024/run-03`.
The game's broader bootstrap diagnostic still says `verdict=FAIL` for its
unported collision/vehicle/frontend checks. This qualifies the road-file
service in a private race path, not full gameplay, Mode 3, physical controls
or an owner installation. The shared lease was released and the installed
EXE hash remained `4D5BE954DEFB7ED457DCF4180CE2DA1BF8CB68F928D965D361D8CB81B16305D8`.
