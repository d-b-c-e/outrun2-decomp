// dbce_shim (windows/shim_dll) through its C ABI over fake COM devices: layout, open refusals, ticks into the frame,
// the owner thread, bounded text, capability, the P21 stop and the spent handle. With a path argument it also loads the
// built DLL and checks its exports (no open: that would create a real DirectInput). No device, game or force.
// Built for x86 (DBCE_SHIM_ORDINARY_ONLY) it runs the ordinary cases and the x86 refusals instead of the latched and
// capture cases, which are x64-only.
#include "dbce_shim_internal.hpp"
#include "dbce_shim_bridge.h"
#include "dbce_shim_extras.h"
#include "ordinary_only.hpp"
#include "dinput_provider.hpp"
#include "shim_fake_com.hpp"
#include <cstring>
#include <vector>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
int checks = 0;
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
const std::string W = "{d71b8350-61b7-11f1-8001-444553540000}", WP = "{0006346e-0000-0000-0000-504944564944}";
const std::string Profile = "[Controls]\nSchema = 1\nProfile = Rig\nRevision = r1\nTransmission = Sequential\n"
    "steer = axis 0 dev=" + W + " prod=" + WP + " range=0..65535 rest=32768 travel=+1\n"
    "throttle = axis 2 dev=" + W + " prod=" + WP + " range=0..65535 rest=0 travel=+1\n"
    "handbrake = button 3 dev=" + W + " prod=" + WP + "\n";
int idx(const char* id) { for (int i = 0; i < DBCE_SHIM_ACTIONS; ++i) if (std::strcmp(dbce_shim_action_name(i), id) == 0) return i; return -1; }
std::filesystem::path scratch() {
    static const auto dir = std::filesystem::temp_directory_path() / ("dbce-shim-abi-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(dir); return dir;
}
fakecom::Input* wheel_input(std::size_t& slot) {
    auto* in = new fakecom::Input;   // fake storage lives to process exit, like the host's own provider
    fakecom::DeviceScript d; check(dbce::shim::guid_parse(W, d.instance) && dbce::shim::guid_parse(WP, d.product), "fixture guids");
    d.name = L"MOZA R12 Base"; d.buttons = 128; d.state.lX = 32768;
    slot = in->add(d); return in;
}
std::string read_text(const std::filesystem::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

void test_layout() {
    check(dbce_shim_abi_version() == 2 && dbce_shim_frame_size() == 296 && sizeof(dbce_shim_frame) == 296, "ABI version and frame size");
#if DBCE_SHIM_ORDINARY_ONLY
    check(dbce_shim_open_args_size() == 80 && sizeof(dbce_shim_open_args) == 80, "open arguments size (x86: 4-byte pointers)");
#else
    check(dbce_shim_open_args_size() == 128 && sizeof(dbce_shim_open_args) == 128, "open arguments size");
#endif
    check(std::strcmp(dbce_shim_action_name(0), "steer") == 0 && std::strcmp(dbce_shim_action_name(30), "gear8") == 0, "action names");
    check(!dbce_shim_action_name(-1) && !dbce_shim_action_name(31), "out-of-range action names are null");
}

void test_profile_inspection() {
    auto inspect = [](const std::string& profile, int action = 0) {
        int32_t needed = 0;
        check(dbce_shim_profile_action_info(profile.data(), int32_t(profile.size()), action, nullptr, 0, &needed) == DBCE_SHIM_OK && needed > 1,
              "pure inspection length without a handle, folder or provider");
        std::vector<char> text(std::size_t(needed), char(0)); int32_t actual = 0;
        check(dbce_shim_profile_action_info(profile.data(), int32_t(profile.size()), action, text.data(), needed, &actual) == DBCE_SHIM_OK &&
              actual == needed && text.back() == 0, "pure inspection bounded fill");
        return std::string(text.data());
    };
    const auto good = inspect(Profile);
    const auto with_camera=inspect(Profile+"cameraZoomIn=button 0 dev="+W+" prod={00000000-0000-0000-0000-000000000001}\n");
    check(with_camera.find("\"status\":\"bound\"")!=std::string::npos&&with_camera.find(WP)!=std::string::npos,"ABI2 camera binding cannot create a legacy inspection product conflict");

    check(good.find("\"status\":\"bound\"") != std::string::npos && good.find("\"kind\":\"axis\"") != std::string::npos &&
          good.find("\"instance\":\"" + W + "\"") != std::string::npos && good.find("\"product\":\"" + WP + "\"") != std::string::npos &&
          good.find(dbce::shim::hex(dbce::shim::sha256(Profile))) != std::string::npos, "configured steering identity and exact full-file hash");
    check(inspect(Profile, idx("brake")).find("\"status\":\"unbound\"") != std::string::npos, "unbound action is explicit");
    for (const auto& invalid : {Profile + "steer =\n", Profile + "brake = nonsense\n", Profile + "unknown = value\n",
                               Profile + "confirm = button 0 dev=" + W + " prod={00000001-0000-0000-0000-504944564944}\n",
                               std::string("[Controls]\nSchema = 2\n"), std::string("[Controls]\nSchema = 1\nProfile = \xC0\x80\n")}) {
        const auto result = inspect(invalid);
        check(result.find("\"status\":\"invalid\"") != std::string::npos && result.find("\"instance\":\"\"") != std::string::npos &&
              result.find("\"product\":\"\"") != std::string::npos, "malformed/full-profile invalidity never exposes a steering target");
    }
    char short_text[4] = {}; int32_t needed = 0;
    check(dbce_shim_profile_action_info(Profile.data(), int32_t(Profile.size()), 0, short_text, 4, &needed) == DBCE_SHIM_OK &&
          short_text[3] == 0 && needed > 4, "truncation is terminated and reports full required length");
    for (int action : {-1, DBCE_SHIM_ACTIONS})
        check(dbce_shim_profile_action_info(Profile.data(), int32_t(Profile.size()), action, nullptr, 0, &needed) == DBCE_SHIM_BAD_ARGUMENT && needed == 0, "invalid action refused");
    check(dbce_shim_profile_action_info(nullptr, 0, 0, nullptr, 0, &needed) == DBCE_SHIM_BAD_ARGUMENT, "null profile refused");
    check(dbce_shim_profile_action_info(Profile.data(), -1, 0, nullptr, 0, &needed) == DBCE_SHIM_BAD_ARGUMENT, "negative profile length refused");
    check(dbce_shim_profile_action_info(Profile.data(), 65537, 0, nullptr, 0, &needed) == DBCE_SHIM_BAD_ARGUMENT, "oversize refused before reading profile pointer");
}

void test_refusals() {
    const auto folder = scratch().wstring(); char why[256]; void* h = nullptr; std::size_t slot = 0;
    auto* in = wheel_input(slot);
    check(dbce_shim_open_with_input(in, folder.c_str(), "abinonce1", Profile.data(), int32_t(Profile.size()), nullptr, nullptr, nullptr, why, 256) == DBCE_SHIM_BAD_ARGUMENT,
          "a null out refuses");
    check(in->refs == 0, "a refused open releases the DirectInput reference it was given");
    in = wheel_input(slot);
    check(dbce_shim_open_with_input(in, (folder + L"\\absent").c_str(), "abinonce1", Profile.data(), int32_t(Profile.size()), nullptr, nullptr, &h, why, 256) == DBCE_SHIM_BAD_ARGUMENT &&
          !h && std::string(why).find("existing folder") != std::string::npos && in->refs == 0, "a missing folder refuses with its reason");
    in = wheel_input(slot);
    check(dbce_shim_open_with_input(in, folder.c_str(), "abinonce1", Profile.data(), int32_t(Profile.size()), "warp=no such action", nullptr, &h, why, 256) == DBCE_SHIM_BAD_ARGUMENT &&
          std::string(why).find("known action") != std::string::npos && in->refs == 0, "an unknown capability action refuses");
    for(const char* action:{"cameraForward","cameraBack","cameraUp","cameraDown","cameraLeft","cameraRight","cameraTiltDown","cameraTiltUp","cameraZoomIn","cameraZoomOut","cameraReset"}){
        for(const std::string& value:{std::string(""),std::string("button 0 dev=")+W+" prod="+WP,std::string("invalid")}){
            in=wheel_input(slot);const auto cameras=std::string("[Controls]\nSchema=1\nProfile=Shared\nRevision=1\nTransmission=Sequential\n")+action+"="+value+"\n";
            check(dbce_shim_open_with_input(in,folder.c_str(),"abinonce1",cameras.data(),int32_t(cameras.size()),nullptr,nullptr,&h,why,256)==DBCE_SHIM_OK&&h,"ABI2 shared camera profile keeps open");
            dbce_shim_frame camera_frame{};camera_frame.struct_size=sizeof camera_frame;
            for(unsigned tick=0;tick<3;++tick)check(dbce_shim_tick(h,&camera_frame)==DBCE_SHIM_OK,"ABI2 camera-only profile ticks");
            check(in->counters.creates==0&&camera_frame.sample_count==0,"ABI2 opens no device used only for camera");
            for(const auto& a:camera_frame.action)check(a.value==0&&a.status==uint8_t(dbce::shim::Status::Unbound)&&!a.injected,"ABI2 legacy prefix stays neutral with shared camera profile");
            char report[4096]{};check(dbce_shim_start_report(h,report,sizeof report,nullptr)==DBCE_SHIM_OK,"shared camera start report readable");
            if(!value.empty()&&value!="invalid")check(std::string(report).find(action)!=std::string::npos&&std::string(report).find("unsupported")!=std::string::npos,"bound camera unsupported reason reported");
            check(dbce_shim_stop(h,why,256,nullptr)==DBCE_SHIM_OK&&in->refs==0,"ABI2 camera-only handle cleans up");
        }
    }
    int32_t needed=0;check(dbce_shim_profile_action_info(Profile.data(),int32_t(Profile.size()),31,nullptr,0,&needed)==DBCE_SHIM_BAD_ARGUMENT,"ABI2 inspection retains31 limit");
    int stack = 0; dbce_shim_frame f{}; f.struct_size = sizeof f;
    check(dbce_shim_tick(&stack, &f) == DBCE_SHIM_BAD_ARGUMENT && dbce_shim_tick(nullptr, &f) == DBCE_SHIM_BAD_ARGUMENT, "an unknown handle refuses unread");
}

void test_lifecycle() {
    const auto dir = scratch() / "host"; std::filesystem::create_directories(dir);
    char why[256] = {}; void* h = nullptr; std::size_t slot = 0; auto* in = wheel_input(slot);
    in->scripts[slot].interface_path=L"\\\\?\\hid#abi";
    check(dbce_shim_open_with_input(in, dir.wstring().c_str(), "abinonce1", Profile.data(), int32_t(Profile.size()), "handbrake=not in this test game", nullptr, &h, why, 256) == DBCE_SHIM_OK && h,
          std::string("open: ") + why);
    char initial[2048] = {};
    check(dbce_shim_device_reports(h, initial, sizeof initial, nullptr) == DBCE_SHIM_OK &&
          std::string(initial).find("\"interfacePath\":\"\"") != std::string::npos, "initial report has no path until input tick adopts the handle");
    dbce_shim_frame f{}; f.struct_size = sizeof f;
    bool live = false;
    for (int n = 0; n < 400 && !live; ++n) { check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "tick"); live = f.action[idx("steer")].status == uint8_t(dbce::shim::Status::Live); if (!live) Sleep(5); }
    check(live && f.abi == DBCE_SHIM_ABI && f.sample_count == 1 && f.run == uint8_t(dbce::shim::Run::None) && !f.latched, "the wheel opens and steer goes Live (ordinary)");
    { std::lock_guard<std::mutex> l(in->m); in->scripts[slot].state.lX = 65535; in->scripts[slot].state.lZ = 65535; }
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK && f.action[idx("steer")].value == 1.f && f.action[idx("throttle")].value == 1.f, "full right and full throttle");
    { std::lock_guard<std::mutex> l(in->m); in->scripts[slot].state.lX = 0; in->scripts[slot].state.lZ = 0; }
    const auto before = f.seq;
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK && f.action[idx("steer")].value == -1.f && f.seq == before + 1, "full left, the next frame");
    check(f.action[idx("handbrake")].status == uint8_t(dbce::shim::Status::Unsupported), "an unsupported action stays Unsupported");
    dbce_shim_frame shortened{}; shortened.struct_size = sizeof shortened - 8;
    check(dbce_shim_tick(h, &shortened) == DBCE_SHIM_BAD_ARGUMENT && dbce_shim_tick(h, nullptr) == DBCE_SHIM_BAD_ARGUMENT, "a wrong frame size refuses");
    int32_t other = 0; std::thread([&] { other = dbce_shim_tick(h, &f); }).join();
    check(other == DBCE_SHIM_WRONG_THREAD, "another thread refuses");
    int32_t needed = 0;
    check(dbce_shim_start_report(h, nullptr, 0, &needed) == DBCE_SHIM_OK && needed > 1, "a length query");
    std::string report(std::size_t(needed), '\0');
    check(dbce_shim_start_report(h, report.data(), needed, nullptr) == DBCE_SHIM_OK && std::strlen(report.c_str()) == std::size_t(needed - 1), "the whole start report");
    char four[4];
    check(dbce_shim_start_report(h, four, 4, &needed) == DBCE_SHIM_OK && std::strlen(four) == 3 && std::memcmp(four, report.data(), 3) == 0, "a truncated report stays terminated");
    check(dbce_shim_start_report(h, nullptr, 4, nullptr) == DBCE_SHIM_BAD_ARGUMENT, "a null buffer with a length refuses");
    char info[1024] = {};
    check(dbce_shim_profile_info(h, info, sizeof info, nullptr) == DBCE_SHIM_OK && std::string(info).find("ok=1") == 0 &&
          std::string(info).find("transmission=Sequential") != std::string::npos && std::string(info).find("revision=r1") != std::string::npos,
          "the profile as Core's parser read it, with its transmission");
    char devices[2048] = {};
    check(dbce_shim_device_reports(h, devices, sizeof devices, nullptr) == DBCE_SHIM_OK && std::string(devices).find(W) != std::string::npos &&
          std::string(devices).find("\"opened\":true") != std::string::npos, "the device table names the open wheel");
    check(std::string(devices).find(R"("interfacePath":"\\\\?\\hid#abi")") != std::string::npos,
          "device JSON exposes the exact live path with JSON escaping");
    { std::lock_guard<std::mutex> l(in->m); in->scripts[slot].reads.push_back({DIERR_UNPLUGGED,DI_OK}); }
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK && dbce_shim_device_reports(h, devices, sizeof devices, nullptr) == DBCE_SHIM_OK &&
          std::string(devices).find("\"interfacePath\":\"\"") != std::string::npos, "ended epoch removes the path from ABI JSON");
    char stopped[512] = {};
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK && std::string(stopped) == "resolved", "the P21 stop resolves");
    check(read_text(dir / "host-shutdown.json").find("\"state\":\"resolved\"") != std::string::npos, "the host's own report says resolved");
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_STOPPED && dbce_shim_stop(h, nullptr, 0, nullptr) == DBCE_SHIM_STOPPED, "a spent handle refuses");
    check(in->counters.force_calls == 0 && in->counters.after_release == 0 && in->refs == 0, "no force call, no use after release, DirectInput released");
}

void test_path_unavailable() {
    for (int failure=0; failure<2; ++failure) {
        const auto dir=scratch()/(failure?"path-failed":"path-empty");std::filesystem::create_directories(dir);
        std::size_t slot=0;auto* in=wheel_input(slot);
        if(failure){in->scripts[slot].interface_path=L"must-not-leak";in->scripts[slot].path_result=E_FAIL;}
        char why[256]{};void* h=nullptr;
        check(dbce_shim_open_with_input(in,dir.wstring().c_str(),"pathnonce",Profile.data(),int32_t(Profile.size()),nullptr,nullptr,&h,why,sizeof why)==DBCE_SHIM_OK,"open with unavailable path remains ABI compatible");
        dbce_shim_frame f{};f.struct_size=sizeof f;bool live=false;
        for(int n=0;n<400&&!live;++n){check(dbce_shim_tick(h,&f)==DBCE_SHIM_OK,"unavailable path tick");live=f.action[idx("steer")].status==uint8_t(dbce::shim::Status::Live);if(!live)Sleep(5);}
        char report[2048]{};
        check(live&&dbce_shim_device_reports(h,report,sizeof report,nullptr)==DBCE_SHIM_OK&&std::string(report).find("\"interfacePath\":\"\"")!=std::string::npos&&std::string(report).find("must-not-leak")==std::string::npos,"missing/failed path query reports empty path with live legacy input");
        check(in->counters.creates==1&&in->counters.path_reads==1&&in->counters.force_calls==0,"ABI metadata query opens no extra device or force");
        check(dbce_shim_stop(h,report,sizeof report,nullptr)==DBCE_SHIM_OK&&std::string(report)=="resolved","metadata fixture stop resolves");
    }
}

// Extension 2 (declared extras), on both architectures: the bounded open, the one tick, the context mask, the read-only
// accessor with identical repeated reads, and an ordinary handle refusing the extension.
void test_extras_abi() {
    check(dbce_shim_extras_abi_version() == 1 && dbce_shim_extras_frame_size() == 296 && sizeof(dbce_shim_extras_frame) == 296, "extension 2 version and frame size");
    const auto dir = scratch() / "extras"; std::filesystem::create_directories(dir);
    const std::string decl = "game test\ncontexts observed race\ntest.boost digital contexts=race\ntest.look stick circle\n";
    char hash[65] = {};
    { const auto h = dbce::shim::hex(dbce::shim::sha256(decl)); std::memcpy(hash, h.data(), 64); }
    const std::string profile = Profile + "\n[Extras]\nSchema = 1\nGame = test\nDeclaration = " + hash + "\n" +
        "test.boost = button 5 dev=" + W + " prod=" + WP + "\ntest.look.x = axis 2 dev=" + W + " prod=" + WP + " range=0..65535 rest=32768 travel=+1\n";
    char why[256] = {}; void* h = nullptr; std::size_t slot = 0;
    // Bounded and strict before anything opens; the DirectInput reference is released.
    auto* in = wheel_input(slot);
    check(dbce_shim_open_extras_with_input(in, dir.wstring().c_str(), "extrasnonce", profile.data(), int32_t(profile.size()), nullptr, nullptr,
                                           decl.data(), 4097, &h, why, 256) == DBCE_SHIM_BAD_ARGUMENT && !h && in->refs == 0, "a declaration length over 4096 refused");
    const std::string nul = std::string("game test\ntest.boost\0 digital\n", 29);
    in = wheel_input(slot);
    check(dbce_shim_open_extras_with_input(in, dir.wstring().c_str(), "extrasnonce", profile.data(), int32_t(profile.size()), nullptr, nullptr,
                                           nul.data(), int32_t(nul.size()), &h, why, 256) == DBCE_SHIM_BAD_ARGUMENT && std::string(why).find("ASCII") != std::string::npos &&
          in->refs == 0, "an embedded NUL refused");
    in = wheel_input(slot);
    check(dbce_shim_open_extras_with_input(in, dir.wstring().c_str(), "extrasnonce", profile.data(), int32_t(profile.size()), nullptr, nullptr,
                                           decl.data(), int32_t(decl.size()), &h, why, 256) == DBCE_SHIM_OK && h, std::string("extras open: ") + why);
    dbce_shim_frame f{}; f.struct_size = sizeof f;
    dbce_shim_extras_frame x{}; x.struct_size = sizeof x;
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_BAD_ARGUMENT, "dbce_shim_tick refuses an extras handle");
    check(dbce_shim_tick_extras(h, 2, &f, &x) == DBCE_SHIM_BAD_ARGUMENT && dbce_shim_extras(h, 0, &x) == DBCE_SHIM_STALE, "an unknown context bit refused, nothing advanced");
    bool live = false;
    for (int n = 0; n < 400 && !live; ++n) {
        check(dbce_shim_tick_extras(h, 1, &f, &x) == DBCE_SHIM_OK, "extras tick");
        live = x.slot[0].status == uint8_t(dbce::shim::Status::Live) && f.action[idx("steer")].status == uint8_t(dbce::shim::Status::Live);
        if (!live) Sleep(5);
    }
    check(live && x.seq == f.seq && x.count == 3 && x.abi == 1 && x.contexts == 1, "one tick: the common frame and the extras share a seq");
    { std::lock_guard<std::mutex> l(in->m); in->scripts[slot].state.rgbButtons[5] = 0x80; }
    check(dbce_shim_tick_extras(h, 1, &f, &x) == DBCE_SHIM_OK && x.slot[0].value == 1.f && (x.down & 1), "a press is a down edge");
    dbce_shim_extras_frame a{}, b{}; a.struct_size = b.struct_size = sizeof a;
    check(dbce_shim_extras(h, f.seq, &a) == DBCE_SHIM_OK && dbce_shim_extras(h, f.seq, &b) == DBCE_SHIM_OK && std::memcmp(&a, &b, sizeof a) == 0 &&
          std::memcmp(&a, &x, sizeof a) == 0 && (a.down & 1), "repeated reads are identical, edges included");
    check(dbce_shim_extras(h, f.seq + 1, &a) == DBCE_SHIM_STALE, "another seq is stale");
    check(dbce_shim_tick_extras(h, 0, &f, &x) == DBCE_SHIM_OK && x.slot[0].status == DBCE_SHIM_CONTEXT_INACTIVE && x.slot[0].value == 0.f &&
          x.slot[1].status == uint8_t(dbce::shim::Status::Live), "out of context: the boost is inactive, the context-free stick is not");
    int32_t needed = 0;
    check(dbce_shim_extras_info(h, nullptr, 0, &needed) == DBCE_SHIM_OK && needed > 1, "extras info length");
    std::string info(std::size_t(needed), '\0');
    check(dbce_shim_extras_info(h, info.data(), needed, nullptr) == DBCE_SHIM_OK && info.find("\"firstSlot\":1") != std::string::npos &&
          info.find("\"contextMode\":\"observed\"") != std::string::npos && info.find("\"acceptedSchema\":1") != std::string::npos,
          "extras info maps ids to slots and names the accepted Schema (the ABI reads 1): " + info.substr(0, 200));
    char stopped[512] = {};
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK && std::string(stopped) == "resolved", "the extras stop resolves");
    check(in->counters.force_calls == 0 && in->refs == 0, "no force call; DirectInput released");
    // Without a declaration: exactly an ordinary handle, which refuses the extension.
    in = wheel_input(slot);
    check(dbce_shim_open_extras_with_input(in, dir.wstring().c_str(), "extrasnonce", Profile.data(), int32_t(Profile.size()), nullptr, nullptr,
                                           nullptr, 0, &h, why, 256) == DBCE_SHIM_OK && h, "no declaration: an ordinary open");
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK && dbce_shim_tick_extras(h, 0, &f, &x) == DBCE_SHIM_BAD_ARGUMENT &&
          dbce_shim_extras(h, f.seq, &x) == DBCE_SHIM_BAD_ARGUMENT && dbce_shim_extras_info(h, nullptr, 0, &needed) == DBCE_SHIM_BAD_ARGUMENT,
          "an ordinary handle refuses the extension");
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK, "ordinary stop");
}

// The ABI reads controls schema 1 (STD-037 item 4; the host's default): a Schema 2 profile opens and applies nothing,
// with the reason in the start report.
void test_schema2_refused() {
    const auto dir = scratch() / "schema2"; std::filesystem::create_directories(dir);
    const std::string two = "[Controls]\nSchema = 2\nProfile = Rig\nRevision = r2\nTransmission = Sequential\n"
        "steer = axis 0 dev=" + W + " prod=" + WP + " range=0..65535 rest=32768 travel=+1 cal=4000..24000..60000\n";
    char why[256] = {}; void* h = nullptr; std::size_t slot = 0; auto* in = wheel_input(slot);
    check(dbce_shim_open_with_input(in, dir.wstring().c_str(), "abinonce1", two.data(), int32_t(two.size()), nullptr, nullptr, &h, why, 256) == DBCE_SHIM_OK && h,
          std::string("schema 2 open: ") + why);
    dbce_shim_frame f{}; f.struct_size = sizeof f;
    for (int n = 0; n < 20; ++n) {
        check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK && f.action[idx("steer")].status == uint8_t(dbce::shim::Status::Unbound), "a Schema 2 steer stays unbound");
        Sleep(1);
    }
    char report[512] = {};
    check(dbce_shim_start_report(h, report, sizeof report, nullptr) == DBCE_SHIM_OK &&
          std::string(report).find("Schema 2 is not supported (this mod reads 1)") != std::string::npos, std::string("the ABI reads schema 1: ") + report);
    char stopped[512] = {};
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK && std::string(stopped) == "resolved", "the refused open stops resolved");
}

dbce_shim_open_args args_for(const std::wstring& folder, uint32_t flags) {
    static std::wstring held; held = folder;
    dbce_shim_open_args a{}; a.struct_size = sizeof a; a.flags = flags; a.folder = held.c_str(); a.nonce = "abinonce1";
    a.profile = Profile.data(); a.profile_len = int32_t(Profile.size()); a.adapter_version = "abi test adapter";
    return a;
}

#if DBCE_SHIM_ORDINARY_ONLY
// x86: open2 (any flags) and the bridge open refuse with the reason and release what they were given; an ordinary
// handle opens, ticks and stops as on x64, while record and the bridge calls refuse it.
void test_x86_refusals() {
    const auto dir = scratch() / "x86"; std::filesystem::create_directories(dir);
    char why[256] = {}; void* h = nullptr; std::size_t slot = 0;
    for (uint32_t flags : {0u, uint32_t(DBCE_SHIM_LATCHED), uint32_t(DBCE_SHIM_CAPTURE), uint32_t(DBCE_SHIM_LATCHED | DBCE_SHIM_CAPTURE)}) {
        auto a = args_for(dir.wstring(), flags); auto* in = wheel_input(slot); h = &slot; why[0] = 0;
        check(dbce_shim_open2_with_input(in, &a, &h, why, 256) == DBCE_SHIM_REFUSED && !h && in->refs == 0 &&
              std::string(why).find("x86 build") != std::string::npos, "open2 refuses on x86 (flags " + std::to_string(flags) + ") and releases its DirectInput");
    }
    h = &slot;
    check(dbce_shim_open2(nullptr, &h, why, 256) == DBCE_SHIM_REFUSED && !h && std::string(why).find("x64-only") != std::string::npos,
          "the exported open2 refuses before creating any DirectInput");
    auto a = args_for(dir.wstring(), 0); h = &slot;
    check(dbce_shim_bridge_open(&a, &h, why, 256) == DBCE_SHIM_REFUSED && !h && std::string(why).find("copied-input bridge") != std::string::npos,
          "the bridge open refuses on x86");
    h = &slot;
    check(dbce_shim_bridge_open_extras(&a, "game fzero\nfzero.boost digital\n", 31, &h, why, 256) == DBCE_SHIM_REFUSED && !h,
          "the additive extras bridge also refuses before any x86 provider exists");
    auto* in = wheel_input(slot);
    check(dbce_shim_open_with_input(in, dir.wstring().c_str(), "x86nonce1", Profile.data(), int32_t(Profile.size()), nullptr, nullptr, &h, why, 256) == DBCE_SHIM_OK && h,
          std::string("x86 ordinary open: ") + why);
    dbce_shim_frame f{}; f.struct_size = sizeof f;
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK && f.abi == DBCE_SHIM_ABI && !f.latched, "x86 ordinary tick");
    const double v[1] = {1};
    check(dbce_shim_record(h, f.seq, v, 1) == DBCE_SHIM_REFUSED, "record refuses on x86");
    check(dbce_shim_bridge_tick(h, nullptr, 0, &f) == DBCE_SHIM_BAD_ARGUMENT && dbce_shim_bridge_devices(h, nullptr, 0) == DBCE_SHIM_BAD_ARGUMENT,
          "an ordinary handle refuses the bridge calls");
    char stopped[512] = {};
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK && std::string(stopped) == "resolved", "the x86 ordinary stop resolves");
    check(dbce_shim_record(h, f.seq, v, 1) == DBCE_SHIM_STOPPED, "a spent handle reports stopped, not refused");
    check(in->counters.force_calls == 0 && in->refs == 0, "no force call; DirectInput released");
}
#else
std::vector<std::string> sample_lines(const std::filesystem::path& source) {
    std::vector<std::string> rows; std::istringstream in(read_text(source)); std::string line;
    while (std::getline(in, line)) if (line.find("\"kind\":\"sample\"") != std::string::npos) rows.push_back(line);
    return rows;
}

void test_open2_refusals() {
    const auto dir = scratch() / "refusals"; std::filesystem::create_directories(dir);
    char why[256]; void* h = nullptr; std::size_t slot = 0;
    auto a = args_for(dir.wstring(), DBCE_SHIM_LATCHED);
    const auto session = (dir / "inject.on").wstring(), batch = (dir / "inject.txt").wstring();
    a.session_file = session.c_str(); a.batch_file = batch.c_str(); a.fences_verified = 1; a.force_absent = 0;
    auto* in = wheel_input(slot);
    check(dbce_shim_open2_with_input(in, &a, &h, why, 256) == DBCE_SHIM_REFUSED && std::string(why).find("force mute") != std::string::npos && in->refs == 0,
          "a latched open without the process-latched force mute is refused");
    a.force_absent = 1; a.fences_verified = 0; in = wheel_input(slot);
    check(dbce_shim_open2_with_input(in, &a, &h, why, 256) == DBCE_SHIM_REFUSED, "a latched open without verified fences is refused");
    a = args_for(dir.wstring(), 8); in = wheel_input(slot);
    check(dbce_shim_open2_with_input(in, &a, &h, why, 256) == DBCE_SHIM_BAD_ARGUMENT, "unknown flags are refused");
    a = args_for(dir.wstring(), DBCE_SHIM_CAPTURE); const auto cap = (dir / "capture").wstring();
    a.capture_folder = cap.c_str(); a.game_id = "Bad Game"; a.hz = 60; a.seconds = 10; in = wheel_input(slot);
    check(dbce_shim_open2_with_input(in, &a, &h, why, 256) == DBCE_SHIM_BAD_ARGUMENT, "a game id outside [a-z0-9.-] is refused");
    a.game_id = "drive-test"; a.game_channels = "speed\nvalid\n"; in = wheel_input(slot);
    check(dbce_shim_open2_with_input(in, &a, &h, why, 256) == DBCE_SHIM_BAD_ARGUMENT && std::string(why).find("reserved") != std::string::npos,
          "the reserved game.valid name is refused");
    a.game_channels = "speed\nspeed\n"; in = wheel_input(slot);
    check(dbce_shim_open2_with_input(in, &a, &h, why, 256) == DBCE_SHIM_BAD_ARGUMENT, "a repeated game channel is refused");
    auto bad = a; bad.struct_size = 64; in = wheel_input(slot);
    check(dbce_shim_open2_with_input(in, &bad, &h, why, 256) == DBCE_SHIM_BAD_ARGUMENT && in->refs == 0, "a wrong argument size is refused");
}

// Latched: Core admits the session, an injected batch drives the frame, physical input stays test-neutral.
void test_latched() {
    const auto dir = scratch() / "latched"; std::filesystem::create_directories(dir);
    const auto session = dir / "inject.on", batch = dir / "inject.txt";
    const auto expires = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() + 600;
    { std::ofstream f(session, std::ios::binary); f << "nonce=abinonce1\nexpires=" << expires << "\n"; }
    auto a = args_for(dir.wstring(), DBCE_SHIM_LATCHED);
    const auto sw = session.wstring(), bw = batch.wstring();
    a.session_file = sw.c_str(); a.batch_file = bw.c_str(); a.fences_verified = 1; a.force_absent = 1;
    char why[256] = {}; void* h = nullptr; std::size_t slot = 0; auto* in = wheel_input(slot);
    check(dbce_shim_open2_with_input(in, &a, &h, why, 256) == DBCE_SHIM_OK && h, std::string("latched open: ") + why);
    { std::lock_guard<std::mutex> l(in->m); in->scripts[slot].state.lX = 65535; }   // physical full right: must not drive
    dbce_shim_frame f{}; f.struct_size = sizeof f; bool waiting = false;
    for (int n = 0; n < 600 && !waiting; ++n) { check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "latched tick"); waiting = f.run == uint8_t(dbce::shim::Run::Waiting); if (!waiting) Sleep(5); }
    check(waiting && f.latched, "the session is admitted and waits for its first batch");
    check(f.action[idx("steer")].status == uint8_t(dbce::shim::Status::TestNeutral) && f.action[idx("steer")].value == 0.f, "physical input is test-neutral");
    { std::ofstream b(batch, std::ios::binary); b << "nonce=abinonce1\nbatch=1\ninject action steer 0.5 ms=3000\n"; }
    bool live = false;
    for (int n = 0; n < 600 && !live; ++n) { check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "latched tick"); live = f.action[idx("steer")].status == uint8_t(dbce::shim::Status::Live); if (!live) Sleep(5); }
    check(live && f.action[idx("steer")].injected && std::fabs(f.action[idx("steer")].value - 0.5f) < 1e-3f && f.run == uint8_t(dbce::shim::Run::Running),
          "the injected batch drives steer, marked injected");
    char stopped[512] = {};
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK, std::string("latched stop resolves: ") + stopped);
    check(std::filesystem::exists(dir / "controls-journal.jsonl") && read_text(dir / "host-shutdown.json").find("\"state\":\"resolved\"") != std::string::npos,
          "the control journal and the host report are in the run folder");
    check(in->counters.force_calls == 0, "no force call");
}

// Capture: one row per Core frame, paired with its game values by seq; gaps, duplicates and bad values stay visible.
void test_capture(const std::filesystem::path& dir, bool expect_checks = true) {
    std::filesystem::create_directories(dir);
    auto a = args_for(dir.wstring(), DBCE_SHIM_CAPTURE);
    const auto cap = (dir / "capture").wstring();
    a.capture_folder = cap.c_str(); a.game_id = "dbce-shim-abi"; a.game_channels = "speed\ngear\n"; a.context = "abi test"; a.hz = 60; a.seconds = 30;
    char why[256] = {}; void* h = nullptr; std::size_t slot = 0; auto* in = wheel_input(slot);
    if (dbce_shim_open2_with_input(in, &a, &h, why, 256) != DBCE_SHIM_OK || !h) throw std::runtime_error(std::string("capture open: ") + why);
    dbce_shim_frame f{}; f.struct_size = sizeof f;
    const double v[2] = {12.5, 3};
    int rows = 0;
    for (int n = 0; n < 8; ++n) {
        if (dbce_shim_tick(h, &f) != DBCE_SHIM_OK) throw std::runtime_error("capture tick"); ++rows;
        if (n == 2) continue;                                                          // no values: game.valid=0
        if (n == 4) { if (expect_checks) check(dbce_shim_record(h, f.seq, v, 1) == DBCE_SHIM_BAD_ARGUMENT, "a wrong count is refused"); continue; }
        if (n == 5) { const double nan[2] = {std::nan(""), 1}; if (expect_checks) check(dbce_shim_record(h, f.seq, nan, 2) == DBCE_SHIM_BAD_ARGUMENT, "a non-finite value is refused"); continue; }
        if (expect_checks) check(dbce_shim_record(h, f.seq + 1, v, 2) == DBCE_SHIM_STALE, "a future frame is refused");
        if (dbce_shim_record(h, f.seq, v, 2) != DBCE_SHIM_OK) throw std::runtime_error("record");
        if (expect_checks) check(dbce_shim_record(h, f.seq, v, 2) == DBCE_SHIM_STALE && dbce_shim_record(h, f.seq - 1, v, 2) == DBCE_SHIM_STALE,
                                 "a second call or an earlier frame is refused");
    }
    char stopped[512] = {};
    const int32_t r = dbce_shim_stop(h, stopped, sizeof stopped, nullptr);
    if (r != DBCE_SHIM_OK || std::string(stopped).find("capture complete") == std::string::npos) throw std::runtime_error(std::string("capture stop: ") + stopped);
    if (!expect_checks) return;
    const auto c = dir / "capture";
    for (const char* m : {"source.jsonl", "worker.tsv", "profile.ini", "bundle.meta", "budget.tsv", "devices.json"})
        check(std::filesystem::exists(c / m), std::string("capture member ") + m);
    check(read_text(c / "bundle.meta") == "dbce.shim.frame-capture@1\ngame=dbce-shim-abi\n", "the generic contract names its game");
    const auto lines = sample_lines(c / "source.jsonl");
    check(int(lines.size()) == rows, "one row per Core frame");
    for (int n = 0; n < rows; ++n) {
        const bool valid = n != 2 && n != 4 && n != 5;
        check(lines[std::size_t(n)].find(std::string("\"game.valid\":") + (valid ? "1" : "0")) != std::string::npos &&
              (!valid || lines[std::size_t(n)].find("\"game.speed\":12.5") != std::string::npos), "row " + std::to_string(n) + " pairs its own values");
    }
    check(read_text(c / "profile.ini") == Profile, "the exact profile bytes");
}

// LATCHED|CAPTURE, DRIVE's actual use (Astra 18330): the injected value reaches the rows, every consumed frame's row
// carries its values, and the final tick after the admission closes is recorded unconsumed (game.valid=0).
void test_latched_capture(const std::filesystem::path& dir, bool expect_checks = true) {
    std::filesystem::create_directories(dir);
    const auto session = dir / "inject.on", batch = dir / "inject.txt";
    const auto expires = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() + 600;
    { std::ofstream f(session, std::ios::binary); f << "nonce=abinonce1\nexpires=" << expires << "\n"; }
    auto a = args_for(dir.wstring(), DBCE_SHIM_LATCHED | DBCE_SHIM_CAPTURE);
    const auto sw = session.wstring(), bw = batch.wstring(), cap = (dir / "capture").wstring();
    a.session_file = sw.c_str(); a.batch_file = bw.c_str(); a.fences_verified = 1; a.force_absent = 1;
    a.capture_folder = cap.c_str(); a.game_id = "dbce-shim-abi"; a.game_channels = "speed\ngear\n"; a.context = "abi latched capture"; a.hz = 240; a.seconds = 30;
    char why[256] = {}; void* h = nullptr; std::size_t slot = 0; auto* in = wheel_input(slot);
    if (dbce_shim_open2_with_input(in, &a, &h, why, 256) != DBCE_SHIM_OK || !h) throw std::runtime_error(std::string("latched capture open: ") + why);
    dbce_shim_frame f{}; f.struct_size = sizeof f;
    const double v[2] = {42.0, 4};
    int rows = 0; bool written = false, live = false;
    for (int n = 0; n < 1200 && !live; ++n) {
        if (dbce_shim_tick(h, &f) != DBCE_SHIM_OK) throw std::runtime_error("latched capture tick");
        ++rows;
        if (dbce_shim_record(h, f.seq, v, 2) != DBCE_SHIM_OK) throw std::runtime_error("latched capture record");
        if (!written && f.run == uint8_t(dbce::shim::Run::Waiting)) {
            std::ofstream b(batch, std::ios::binary); b << "nonce=abinonce1\nbatch=1\ninject action steer 0.5 ms=3000\n"; written = true;
        }
        live = f.action[idx("steer")].status == uint8_t(dbce::shim::Status::Live) && f.action[idx("steer")].injected;
        if (!live) Sleep(5);
    }
    if (!live) throw std::runtime_error("the injected batch never drove steer");
    char stopped[512] = {};
    if (dbce_shim_stop(h, stopped, sizeof stopped, nullptr) != DBCE_SHIM_OK || std::string(stopped).find("capture complete") == std::string::npos)
        throw std::runtime_error(std::string("latched capture stop: ") + stopped);
    if (!expect_checks) return;
    const auto lines = sample_lines(dir / "capture" / "source.jsonl");
    check(int(lines.size()) == rows + 1, "one row per consumed frame plus the final tick");
    for (int n = 0; n < rows; ++n)
        check(lines[std::size_t(n)].find("\"game.valid\":1") != std::string::npos && lines[std::size_t(n)].find("\"game.speed\":42") != std::string::npos,
              "consumed row " + std::to_string(n) + " carries its values");
    check(lines.back().find("\"game.valid\":0") != std::string::npos && lines.back().find("\"game.speed\":0") != std::string::npos,
          "the final tick after the admission closed is recorded unconsumed");
    const auto& last = lines[std::size_t(rows - 1)];
    const auto at = last.find("\"action.steer.value\":");
    const double steer = at == std::string::npos ? 0 : std::strtod(last.c_str() + at + 21, nullptr);
    check(last.find("\"action.steer.injected\":1") != std::string::npos && std::fabs(steer - 0.5) < 1e-3,
          "the injected steer is in the recorded frame (" + std::to_string(steer) + ")");
    check(std::filesystem::exists(dir / "controls-journal.jsonl") && read_text(dir / "capture" / "bundle.meta") == "dbce.shim.frame-capture@1\ngame=dbce-shim-abi\n",
          "the journal and the generic capture side by side in the run folder");
    check(in->counters.force_calls == 0, "no force call");
}
#endif
}

#if !DBCE_SHIM_ORDINARY_ONLY
int capture_child(const std::filesystem::path& run, bool latched) {   // for the out-of-process seal check (shim_dll_seal.py)
    try { if (latched) test_latched_capture(run, false); else test_capture(run, false); return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
}
#endif
namespace {
// Every export both headers declare, by its plain name (the x86 build's __cdecl names are undecorated too).
void test_dll(const std::wstring& path) {
    HMODULE m = LoadLibraryW(path.c_str()); check(m != nullptr, "the built DLL loads");
    for (const char* name : {"dbce_shim_abi_version", "dbce_shim_frame_size", "dbce_shim_open_args_size", "dbce_shim_action_name", "dbce_shim_open",
                             "dbce_shim_open2", "dbce_shim_tick", "dbce_shim_record", "dbce_shim_start_report", "dbce_shim_device_reports",
                             "dbce_shim_profile_info", "dbce_shim_stop", "dbce_shim_profile_action_info",
                             "dbce_shim_bridge_abi_version", "dbce_shim_bridge_device_size", "dbce_shim_bridge_sample_size", "dbce_shim_bridge_open",
                             "dbce_shim_bridge_devices", "dbce_shim_bridge_devices_with_paths", "dbce_shim_bridge_tick",
                             "dbce_shim_bridge_open_extras", "dbce_shim_bridge_tick_extras",
                             "dbce_shim_extras_abi_version", "dbce_shim_extras_frame_size", "dbce_shim_open_extras", "dbce_shim_tick_extras",
                             "dbce_shim_extras", "dbce_shim_extras_info"})
        check(GetProcAddress(m, name) != nullptr, std::string("export ") + name);
    auto proc = [&](const char* n) { return reinterpret_cast<void (*)()>(GetProcAddress(m, n)); };   // the usual FARPROC idiom
    auto version = reinterpret_cast<int32_t (*)()>(proc("dbce_shim_abi_version"));
    auto size = reinterpret_cast<int32_t (*)()>(proc("dbce_shim_frame_size"));
    auto name = reinterpret_cast<const char* (*)(int32_t)>(proc("dbce_shim_action_name"));
    check(version() == 2 && size() == 296 && std::strcmp(name(0), "steer") == 0, "the DLL's ABI agrees with this build");
#if DBCE_SHIM_ORDINARY_ONLY
    auto args_size = reinterpret_cast<int32_t (*)()>(proc("dbce_shim_open_args_size"));
    auto open2 = reinterpret_cast<int32_t (*)(const dbce_shim_open_args*, void**, char*, int32_t)>(proc("dbce_shim_open2"));
    char why[256] = {}; void* h = why;
    check(args_size() == 80 && open2(nullptr, &h, why, 256) == DBCE_SHIM_REFUSED && !h && std::string(why).find("x86 build") != std::string::npos,
          "the built x86 DLL is ordinary only");
#endif
}
}

int main(int argc, char** argv) {
#if !DBCE_SHIM_ORDINARY_ONLY
    if (argc == 3 && std::string(argv[1]) == "--capture-child") return capture_child(std::filesystem::path(argv[2]), false);
    if (argc == 3 && std::string(argv[1]) == "--latched-capture-child") return capture_child(std::filesystem::path(argv[2]), true);
#endif
    try {
        test_layout(); test_profile_inspection(); test_refusals(); test_lifecycle(); test_path_unavailable(); test_schema2_refused(); test_extras_abi();
#if DBCE_SHIM_ORDINARY_ONLY
        test_x86_refusals();
#else
        test_open2_refusals(); test_latched(); test_capture(scratch() / "capture-run"); test_latched_capture(scratch() / "latched-capture-run");
#endif
        if (argc == 2) test_dll(std::filesystem::path(argv[1]).wstring());
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << "\n"; return 1; }
    std::error_code ec; std::filesystem::remove_all(scratch(), ec);
    std::cout << "shim ABI: " << checks << " checks passed\n";
    return 0;
}
