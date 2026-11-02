"""Prepare the exact pre-extension codecs and ABI2 serializer for frozen CPU fixtures."""
import argparse, hashlib, json, subprocess, tarfile
from pathlib import Path
BASE="be4a46d9ac3add19836f104de90c63c3f74078e2"
a=argparse.ArgumentParser();a.add_argument("--out",type=Path,required=True);args=a.parse_args()
out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
archive=out/"baseline.tar"
with archive.open("wb") as f: subprocess.run(["git","archive","--format=tar",BASE,"native/controls","native/shim","native/session"],stdout=f,check=True)
source=out/"source";source.mkdir()
with tarfile.open(archive) as f:f.extractall(source,filter="data")
cpp=source/"native/shim/shim_dll/dbce_shim.cpp";text=cpp.read_text()
start=text.index("        frame->abi = DBCE_SHIM_ABI;");end=text.index("        // Extension 4:",start)
body=text[start:end]
(out/"baseline_serializer.inc").write_text(body)
program=(Path(__file__).parent/"baseline_main.cpp").read_text()
(out/"main.cpp").write_text(program)
paths=[source/"native/shim/shim",source/"native/shim/host/recorder",source/"native/shim/shim_dll",source/"native/controls",source/"native/session"]
cmake='cmake_minimum_required(VERSION 3.20)\nproject(camera_baseline CXX)\nadd_executable(camera_baseline main.cpp "'+(source/"native/shim/shim/parse.cpp").as_posix()+'")\ntarget_compile_features(camera_baseline PRIVATE cxx_std_17)\ntarget_compile_definitions(camera_baseline PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)\ntarget_compile_options(camera_baseline PRIVATE /W4 /WX /EHsc)\ntarget_include_directories(camera_baseline PRIVATE '+" ".join('"'+x.as_posix()+'"' for x in paths)+')\n'
(out/"CMakeLists.txt").write_text(cmake)
(out/"provenance.json").write_text(json.dumps({"commit":BASE,"serializerSourceSha256":hashlib.sha256(cpp.read_bytes()).hexdigest(),"extractedSerializerSha256":hashlib.sha256(body.encode()).hexdigest(),"scope":"exact baseline channel/observer headers and actual ABI2 tick serializer fragment; CPU only, no DLL/device/game initialization"},indent=2)+"\n")
print(out)
