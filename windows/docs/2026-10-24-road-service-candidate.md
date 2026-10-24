# October 24 road-service private candidate

Source `6dd3a55e4286b486eef2c06dbf36cbb4d9179cef` adds a pending
road-table read to the game's ordinary file-service step. Tweaks fast-load
Mode 3 remains unavailable; other file routes and the frame-deadline pump
are not implemented.

The ClangCL Release desktop and focused `road_file_service` CTest passed.
The test checks pending submission, one successful read, idle, reset
cancellation, and a failed read. The existing package-tool suite passed 8/8.

Private package:
`E:/outrun2-research/private-candidate-6dd3a55-20261024/candidate.zip`
(SHA-256 `51B6FB50C518CCD23B4B96700E3DD7F4F1870944A13146F597F6552C10663A17`).
The 14-member package binds the source commit to an operator-supplied
`OutRunDesktop.exe` (SHA-256
`478C78A8D9F9472C15159EA09D895B946823B4FED06BAD316214EE90A9742A65`).
Build logs are beside the earlier `e4e0ed5` package in
`E:/outrun2-research/private-candidate-e4e0ed5-20261024/road-build2.log`.

This is ready for a private road-loading smoke test. It has not been
launched or installed in the owner's game. The earlier `e4e0ed5` private
candidate and installed owner version remain unchanged.
