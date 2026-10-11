// dbce_shim (windows/shim_dll) through its C ABI over fake COM devices: layout, open refusals, ticks into the frame,
// the owner thread, bounded text, capability, the P21 stop and the spent handle. With a path argument it also loads the
// built DLL and checks its exports (no open: that would create a real DirectInput). No device, game or force.
#include "dbce_shim_internal.hpp"
#include "dinput_provider.hpp"
#include "shim_fake_com.hpp"
#include <cstring>
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
    check(dbce_shim_abi_version() == 1 && dbce_shim_frame_size() == 296 && sizeof(dbce_shim_frame) == 296, "ABI version and frame size");
    check(std::strcmp(dbce_shim_action_name(0), "steer") == 0 && std::strcmp(dbce_shim_action_name(30), "gear8") == 0, "action names");
    check(!dbce_shim_action_name(-1) && !dbce_shim_action_name(31), "out-of-range action names are null");
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
    int stack = 0; dbce_shim_frame f{}; f.struct_size = sizeof f;
    check(dbce_shim_tick(&stack, &f) == DBCE_SHIM_BAD_ARGUMENT && dbce_shim_tick(nullptr, &f) == DBCE_SHIM_BAD_ARGUMENT, "an unknown handle refuses unread");
}

void test_lifecycle() {
    const auto dir = scratch() / "host"; std::filesystem::create_directories(dir);
    char why[256] = {}; void* h = nullptr; std::size_t slot = 0; auto* in = wheel_input(slot);
    check(dbce_shim_open_with_input(in, dir.wstring().c_str(), "abinonce1", Profile.data(), int32_t(Profile.size()), "handbrake=not in this test game", nullptr, &h, why, 256) == DBCE_SHIM_OK && h,
          std::string("open: ") + why);
    dbce_shim_frame f{}; f.struct_size = sizeof f;
    bool live = false;
    for (int n = 0; n < 400 && !live; ++n) { check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "tick"); live = f.action[idx("steer")].status == uint8_t(dbce::shim::Status::Live); if (!live) Sleep(5); }
    check(live && f.abi == 1 && f.sample_count == 1 && f.run == uint8_t(dbce::shim::Run::None) && !f.latched, "the wheel opens and steer goes Live (ordinary)");
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
    char stopped[512] = {};
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK && std::string(stopped) == "resolved", "the P21 stop resolves");
    check(read_text(dir / "host-shutdown.json").find("\"state\":\"resolved\"") != std::string::npos, "the host's own report says resolved");
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_STOPPED && dbce_shim_stop(h, nullptr, 0, nullptr) == DBCE_SHIM_STOPPED, "a spent handle refuses");
    check(in->counters.force_calls == 0 && in->counters.after_release == 0 && in->refs == 0, "no force call, no use after release, DirectInput released");
}

void test_dll(const std::wstring& path) {
    HMODULE m = LoadLibraryW(path.c_str()); check(m != nullptr, "the built DLL loads");
    for (const char* name : {"dbce_shim_abi_version", "dbce_shim_frame_size", "dbce_shim_action_name", "dbce_shim_open", "dbce_shim_tick",
                             "dbce_shim_start_report", "dbce_shim_device_reports", "dbce_shim_profile_info", "dbce_shim_stop"})
        check(GetProcAddress(m, name) != nullptr, std::string("export ") + name);
    auto proc = [&](const char* n) { return reinterpret_cast<void (*)()>(GetProcAddress(m, n)); };   // the usual FARPROC idiom
    auto version = reinterpret_cast<int32_t (*)()>(proc("dbce_shim_abi_version"));
    auto size = reinterpret_cast<int32_t (*)()>(proc("dbce_shim_frame_size"));
    auto name = reinterpret_cast<const char* (*)(int32_t)>(proc("dbce_shim_action_name"));
    check(version() == 1 && size() == 296 && std::strcmp(name(0), "steer") == 0, "the DLL's ABI agrees with this build");
}
}

int main(int argc, char** argv) {
    try {
        test_layout(); test_refusals(); test_lifecycle();
        if (argc == 2) test_dll(std::filesystem::path(argv[1]).wstring());
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << "\n"; return 1; }
    std::error_code ec; std::filesystem::remove_all(scratch(), ec);
    std::cout << "shim ABI: " << checks << " checks passed\n";
    return 0;
}
