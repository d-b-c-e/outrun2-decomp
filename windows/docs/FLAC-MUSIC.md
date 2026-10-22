# Optional desktop FLAC music qualification

Existing default-off AllowWAV/AllowFLAC settings, MusicStream, production
Sound lookup and MenuAudio are reused. Enabled and present FLAC wins over WAV
over stock Ogg. Selected read/decode failures refuse without another format.
Whole-file ownership, the 64 MiB input limit and 96000-frame mix work bound
remain unchanged. No loop-tag, CD switching, default or platform expansion.

Source commits:
- 9368f4b615ae484f512a3d6dcc3824dceadc934e: expose the production lookup.
- 4df2d1e50bb0f21d921739f70a3443741e9614a9: isolated dependency recipe, explicit compiler builtins preservation,
  disposable Sound lookup/MenuAudio fixture.
- Decoder baseline: 91e184508221e1700265753f70b5bb6e951ebfb8.
- Retained numeric fixture: 9deb12b667a062295e11bc3a13448cca5c530635.

## Offline dependency recipe

Run from this checkout. The qualified Bink-only prefix at
E:/Source/research/outrun2-decomp/build-deps/ffmpeg remains read-only.
Reuse sibling zlib and Vorbis prefixes read-only. All new dependency output,
owned fixtures and build artifacts stay in ignored work/flac-integration/
and build-flac-integration/.

~~~powershell
New-Item -ItemType Directory -Force work/flac-integration/deps
Copy-Item E:/Source/research/outrun2-decomp/build-deps/ffmpeg-7.1.1.tar.xz work/flac-integration/deps
powershell -NoProfile -File xbox/tools/build-dependencies.ps1 -Out E:/Source/.fleet-worktrees/20261022/outrun/work/flac-integration/deps -Bash E:/msys64/usr/bin/bash.exe -VsDir "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools" -Music -FFmpegOnly -Offline
~~~

The script verifies FFmpeg 7.1.1 archive SHA-256
733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1.
Missing archives and mismatches fail closed. The separate output prefix is
work/flac-integration/deps/ffmpeg-audio. The minimal static LGPL configuration
retains Bink demux/video/RDFT/DCT audio, swscale and swresample and adds
FLAC/WAV demuxers, FLAC parser/decoder and PCM U8/S16LE/S24LE/S32LE/F32LE.
Network, programs, GPL/nonfree and hardware acceleration remain disabled.
FFmpegOnly avoids rebuilding zlib/Vorbis. Use a fresh isolated output folder;
never configure inside the qualified dependency source directory.

Evidence: work/flac-integration/dependency-build.log, deps/ffmpeg-build.cmd,
deps/ffmpeg-7.1.1/ffbuild/config.mak (all dependency paths relative to
work/flac-integration). VS 2022 BuildTools clang-cl 19, x64 MSVC ABI, /MD.
No network request, package installation or owner installation/configuration.

## Reproduce owned inputs

The installed ffmpeg is only an encoder for generated arithmetic PCM; it
is not the decoder being qualified. Save this as
work/flac-integration/generate.py and run python on it from the checkout.

~~~python
from pathlib import Path
import struct,subprocess
out=Path("work/flac-integration/fixtures");out.mkdir(parents=True,exist_ok=True)
for name,rate,ch,bits,flt,enc in [("mono-48000-s16",48000,1,16,False,16),("stereo-24000-s24",24000,2,24,False,32),("stereo-96000-s32",96000,2,32,False,32),("mono-8000-u8",8000,1,8,False,16),("stereo-192000-f32",192000,2,32,True,32)]:
 data=bytearray()
 for f in range(257):
  for c in range(ch):
   v=((f*37+c*101)%127-63)*256
   if flt:data+=struct.pack("<f",v/32768)
   elif bits==8:data+=bytes([v//256+128])
   else:data+=int(v*(1<<(bits-16))).to_bytes(bits//8,"little",signed=True)
 wave=b"RIFF"+struct.pack("<I",36+len(data)+(len(data)&1))+b"WAVEfmt "+struct.pack("<IHHIIHH",16,3 if flt else 1,ch,rate,rate*ch*bits//8,ch*bits//8,bits)+b"data"+struct.pack("<I",len(data))+data+bytes(len(data)&1)
 (out/(name+".wav")).write_bytes(wave)
 subprocess.run(["ffmpeg","-v","error","-y","-i",str(out/(name+".wav")),"-c:a","flac","-sample_fmt","s16" if enc==16 else "s32",str(out/(name+".flac"))],check=True)
subprocess.run(["ffmpeg","-v","error","-y","-i",str(out/"mono-48000-s16.flac"),"-c:a","copy","-metadata","LOOPSTART=3",str(out/"tagged-loop.flac")],check=True)
subprocess.run(["ffmpeg","-v","error","-y","-i",str(out/"mono-48000-s16.wav"),"-c:a","libvorbis",str(out/"stock.ogg")],check=True)
print("Owned PCM fixture generation PASS")
~~~

## Configure and focused checks

The successful production configure uses Ninja inside vcvars64. Earlier VS
generator compiler discovery/FileTracker failures remain in
work/flac-integration/desktop-configure*.log; they are not product passes.
Save this batch file as work/flac-integration/configure.cmd and run it:

~~~bat
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin;%PATH%
cmake -S . -B build-flac-integration/audio -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang-cl -DOR2_XBOX=ON -DZLIB_INCLUDE_DIR=E:/Source/research/outrun2-decomp/build-deps/zlib/include -DZLIB_LIBRARY=E:/Source/research/outrun2-decomp/build-deps/zlib/lib/zlibstatic.lib -DOR2_XBOX_VORBIS_ROOT=E:/Source/research/outrun2-decomp/build-deps/vorbis -DOR2_XBOX_FFMPEG_ROOT=E:/Source/.fleet-worktrees/20261022/outrun/work/flac-integration/deps/ffmpeg-audio "-DOR2_CLANG_BUILTINS=C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/lib/clang/19/lib/windows/clang_rt.builtins-x86_64.lib" -DOR2_MUSIC_LOOKUP_FIXTURE_DIR=E:/Source/.fleet-worktrees/20261022/outrun/work/flac-integration/fixtures
exit /b %errorlevel%
~~~

Run the heavy build only under the existing serialized build lease:

~~~powershell
cmake --build build-flac-integration/audio --parallel 4 --target OutRunDesktop test_music_lookup
ctest --test-dir build-flac-integration/audio --output-on-failure -R '^music_lookup$'
~~~

The existing numeric fixture uses the exact same audio-enabled prefix.
Run these commands inside vcvars64 with clang-cl on PATH:

~~~bat
cmake -S . -B build-flac-integration/numeric-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang-cl -DOR2_MUSIC_FIXTURE_ONLY=ON -DOR2_MUSIC_FFMPEG_ROOT=E:/Source/.fleet-worktrees/20261022/outrun/work/flac-integration/deps/ffmpeg-audio -DOR2_MUSIC_FIXTURE_DIR=E:/Source/.fleet-worktrees/20261022/outrun/work/flac-integration/fixtures
cmake --build build-flac-integration/numeric-ninja --parallel 1 --target test_music_stream
ctest --test-dir build-flac-integration/numeric-ninja --output-on-failure -R "^music_stream$"
~~~

Numeric fixture PASS: 18,807,664 checks; CTest 1/1. It asserts actual FLAC
demux/decoder, every required WAV PCM codec and all original Bink components
in the linked static libraries. Evidence: work/flac-integration/numeric.log,
numeric-result.txt and build-flac-integration/numeric-ninja/Testing/Temporary/LastTest.log.
Older passing artifacts remain intact; unrelated suites were not rerun.

Desktop link and lookup fixture (2026-10-22, source
459b8c6545df7ad8fbfbf27de67c92a31b89f11d, clean tree): the earlier 11:47
OutRunDesktop.exe in build-flac-integration/audio predated later desktop
commits, so it was relinked; music/runtime/CMake sources were unchanged since
4099259. Run under coordinator kobe's borrowed shared-build lease
(lease=9348bbdc..., asserted with Stage-RigLease.ps1; parent release requested
from kobe after the run, lease file never written by this task), same
cache: FFmpeg 7.1.1 audio prefix above and Clang 19 builtins. No dependency
rebuild, configure change or decoder rerun.

- Link: work/flac-integration/link.cmd (vcvars64, the build command above,
  incremental, 39 steps incl. CMake regenerate) exit 0; link.log.
  OutRunDesktop.exe SHA-256
  eb1ac83b053b2281a665e7f9d317161e104a54eb90f3cdad431bd9fcab031206,
  test_music_lookup.exe
  237f65db05517425945674e1c3770b6a8247176dc5a77e58cfd1a9950ca07675.
- Lookup: the ctest command above, 1/1 PASS, "music_lookup: 843 checks
  PASS"; lookup.log and lookup-LastTest.log. No source repair was needed.
- Cleanup: the disposable work/flac-integration/lookup root is empty; no
  ninja/clang-cl/lld-link/cmake/ctest/test process remained.

Link and lookup results are separate from each other and from audible
runtime acceptance, which was not attempted.

The fixture creates a new disposable Sound child and removes only that child.
Coverage: mixed-case same-stem selection, FLAC/WAV/Ogg precedence, disabled/
absent replacement stock bytes, stock MenuAudio decoding, corrupt/unreadable/
oversized selected FLAC refusal, decoder failure, 800 exact whole-file-loop
PCM frames, stop/reopen/reset and additive effect mixing. Unreadability uses
an exclusive Windows file handle, without changing an owner's ACL.

No game launch, audible listening, rendered UI, owner settings persistence,
XAudio2 output, Bink movie playback or physical hardware acceptance.
Offline decoder/PCM checks do not establish these runtime outcomes.

The fixture translation unit compiled directly with the generated production
clang-cl command, /W4 /WX, C++20 and Release /MD (PASS;
work/flac-integration/direct-compile.cmd and direct-compile.log).
This checks compilation only. A preceding Ninja object-target invocation
unexpectedly scheduled transitive engine work; its owned Ninja process was
stopped on discovery. The interrupted build/warnings remain in
work/flac-integration/fixture-compile.log. A subsequent process inventory
found no ninja/clang-cl/lld-link command referencing flac-integration.
No integration execution or desktop-link success is inferred from partial objects.
Dependency library hashes are in work/flac-integration/dependency-hashes.json.

The shared checkout additionally contains peer WAV host callback work committed
as e76fbc64f40a38c0ab8c9a6d8c0a6f628e843876. It was not staged or committed by
this FLAC task. Existing work directories and prior passing evidence were preserved.
