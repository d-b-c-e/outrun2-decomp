// Slice 4 P29: short real-device read only. No game, adapter, injection admission, effect or force implementation.
// The reviewed host opens on its provider worker; this thread runs Core::tick and stops the host.
#include "p21_provider_host/host.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>

namespace fs = std::filesystem;
using namespace dbce::shim;

// This probe does not record a session. There is no recorder worker to drain, and it cannot make complete.tsv.
struct NoCapture {
    void finish() {}
    p21_spike::Recorder::Drained drain(p21_spike::Clock::time_point) { return {p21_spike::RecOutcome::Complete, true}; }
};
using Host = p21k::BasicProviderHost<NoCapture>;
static void need(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

int wmain(int argc, wchar_t** argv) {
    try {
        need(argc == 4 && std::wstring(argv[1]) == L"--read-only-device-probe",
            "usage: readonly_provider --read-only-device-probe <profile.ini> <new result folder>; hold rig lease first");
        const fs::path profile_path(argv[2]), result(argv[3]);
        need(fs::is_regular_file(profile_path) && fs::file_size(profile_path) <= MaxProfileBytes, "bounded profile required");
        std::ifstream stream(profile_path, std::ios::binary);
        const std::string profile((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        need(read_profile(profile).ok, "profile must parse before opening DirectInput");
        need(fs::create_directory(result), "result folder must be new");

        // A valid owned top-level HWND, never shown or focused. Do not use the owner's window as a cooperative target.
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"DbceReadonlyProvider";
        need(RegisterClassW(&wc) != 0, "window class failed");
        HWND window = CreateWindowExW(0, wc.lpszClassName, L"DBCE read-only provider", WS_OVERLAPPED, 0, 0, 1, 1,
                                     nullptr, nullptr, wc.hInstance, nullptr);
        need(window != nullptr, "hidden cooperative window failed");
        IDirectInput8W* input = nullptr;
        need(SUCCEEDED(DirectInput8Create(wc.hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                        reinterpret_cast<void**>(&input), nullptr)) && input != nullptr, "DirectInput8Create failed");
        auto host = std::make_unique<Host>(result.wstring(), "readonly-provider-p29", input, profile, false,
                                          std::make_unique<NoCapture>(), Capability{}, window, 100000);
        std::vector<Frame> frames; frames.reserve(320);
        const auto deadline = p21_spike::Clock::now() + std::chrono::seconds(12);
        // Includes opening frames. All file output is after stop; each tick's read times come from the real core.
        while (frames.size() < 300 && p21_spike::Clock::now() < deadline) {
            MSG message{};
            while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            frames.push_back(host->tick());
            Sleep(16);
        }
        const auto report = host->stop();
        if (!report.resolved) host->forced_exit();
        host.reset(); // All driver work has ended before the cooperative HWND is destroyed.
        need(DestroyWindow(window) != 0, "window destroy failed");
        need(UnregisterClassW(wc.lpszClassName, wc.hInstance) != 0, "window class cleanup failed");

        std::ofstream samples(result / "samples.tsv", std::ios::binary);
        samples << "frame\ttick_us\tboundary_us\tdevice\tepoch\tsequence\tread_start_us\tread_end_us\tpoll\tstate\tvalid\tstale";
        for (int i = 0; i < 8; ++i) samples << "\taxis" << i;
        for (int i = 0; i < 4; ++i) samples << "\tpov" << i;
        samples << "\tbuttons_hex\n";
        std::ofstream actions(result / "actions.tsv", std::ios::binary);
        actions << "frame\taction\tstatus\tvalue\tsample\n" << std::setprecision(9);
        std::set<std::uint32_t> valid_devices;
        for (const auto& frame : frames) {
            need(frame.run == Run::None && !frame.latched && !frame.suppress_native, "unexpected injection or run state");
            for (const auto& s : frame.samples) {
                need(!s.injected.any() && std::equal(std::begin(s.physical.axes), std::end(s.physical.axes), std::begin(s.delivered.axes)) &&
                     std::equal(std::begin(s.physical.povs), std::end(s.physical.povs), std::begin(s.delivered.povs)) &&
                     std::equal(std::begin(s.physical.buttons), std::end(s.physical.buttons), std::begin(s.delivered.buttons)), "physical sample changed");
                if (s.valid && !s.stale) valid_devices.insert(s.device);
                samples << frame.seq << '\t' << frame.tick_us << '\t' << frame.boundary_us << '\t' << s.device << '\t' << s.epoch << '\t' << s.seq
                        << '\t' << s.read_start_us << '\t' << s.read_end_us << '\t' << int(s.poll) << '\t' << int(s.state) << '\t' << s.valid << '\t' << s.stale;
                for (const auto a : s.physical.axes) samples << '\t' << a;
                for (const auto p : s.physical.povs) samples << '\t' << p;
                samples << '\t' << std::hex << std::setfill('0');
                for (const auto b : s.physical.buttons) samples << std::setw(2) << unsigned(b);
                samples << std::dec << '\n';
            }
            for (int i = 0; i < ActionCount; ++i) {
                const auto& a = frame.action[std::size_t(i)];
                need(!a.injected, "injected action in read-only probe");
                actions << frame.seq << '\t' << action_name(i) << '\t' << status_name(a.status) << '\t' << a.value << '\t' << a.sample << '\n';
            }
        }
        samples.close(); actions.close();
        need(samples.good() && actions.good(), "evidence output failed");
        need(frames.size() == 300 && valid_devices.size() >= 2, "expected 300 ticks and at least two valid device streams");
        std::cout << "PASS: 300 real Core ticks; " << valid_devices.size() << " devices read; resolved shutdown. Read-only, no game or force.\n";
        return 0;
    } catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
}
