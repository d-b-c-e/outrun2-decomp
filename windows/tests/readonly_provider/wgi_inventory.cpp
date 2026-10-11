// P30 corroboration only: WGI lists and metadata. No readings, vibration, force, game, input injection or windows.
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Data.Json.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
namespace wgi=winrt::Windows::Gaming::Input;
namespace js=winrt::Windows::Data::Json;
namespace fs=std::filesystem;

int wmain(int argc,wchar_t** argv){
    try{
        if(argc!=3||std::wstring(argv[1])!=L"--list-only")throw std::runtime_error("usage: wgi_inventory --list-only <new folder>");
        const fs::path result(argv[2]);
        if(!fs::create_directory(result))throw std::runtime_error("result folder must be new");
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        // Three observations permit asynchronous device discovery to settle; none imply an exact DirectInput instance
        // join. VID/PID are hardware products. The non-roamable WGI identity is recorded separately, never substituted.
        js::JsonArray snapshots;
        for(int sample=0;sample<3;++sample){
            if(sample)std::this_thread::sleep_for(std::chrono::seconds(1));
            js::JsonObject snapshot;snapshot.Insert(L"index",js::JsonValue::CreateNumberValue(sample));
            js::JsonArray raw,pads;
            for(const auto& d:wgi::RawGameController::RawGameControllers()){
                js::JsonObject item;
                item.Insert(L"name",js::JsonValue::CreateStringValue(d.DisplayName()));
                item.Insert(L"id",js::JsonValue::CreateStringValue(d.NonRoamableId()));
                item.Insert(L"vid",js::JsonValue::CreateNumberValue(d.HardwareVendorId()));
                item.Insert(L"pid",js::JsonValue::CreateNumberValue(d.HardwareProductId()));
                item.Insert(L"axes",js::JsonValue::CreateNumberValue(d.AxisCount()));
                item.Insert(L"buttons",js::JsonValue::CreateNumberValue(d.ButtonCount()));
                item.Insert(L"switches",js::JsonValue::CreateNumberValue(d.SwitchCount()));
                item.Insert(L"isGamepad",js::JsonValue::CreateBooleanValue(bool(wgi::Gamepad::FromGameController(d))));
                raw.Append(item);
            }
            for(const auto& p:wgi::Gamepad::Gamepads()){
                const auto d=wgi::RawGameController::FromGameController(p);
                js::JsonObject item;item.Insert(L"hasRawController",js::JsonValue::CreateBooleanValue(bool(d)));
                if(d){
                    item.Insert(L"id",js::JsonValue::CreateStringValue(d.NonRoamableId()));
                    item.Insert(L"vid",js::JsonValue::CreateNumberValue(d.HardwareVendorId()));
                    item.Insert(L"pid",js::JsonValue::CreateNumberValue(d.HardwareProductId()));
                }
                pads.Append(item);
            }
            snapshot.Insert(L"raw",raw);snapshot.Insert(L"gamepads",pads);snapshots.Append(snapshot);
        }
        js::JsonObject report;report.Insert(L"schema",js::JsonValue::CreateStringValue(L"dbce.shim.wgi-inventory@1"));
        report.Insert(L"snapshots",snapshots);
        report.Insert(L"scope",js::JsonValue::CreateStringValue(L"Read-only WGI metadata; product corroboration, not DirectInput instance identity or game-consumer qualification"));
        std::ofstream file(result/L"wgi.json",std::ios::binary);file<<winrt::to_string(report.Stringify());file.close();
        if(!file.good())throw std::runtime_error("inventory output failed");
        // All WGI objects leave scope before apartment shutdown/process exit.
        std::cout<<"PASS: three read-only WGI inventories; no game or force.\n";
        return 0;
    }catch(const winrt::hresult_error& ex){std::cerr<<winrt::to_string(ex.message())<<'\n';return 1;}
    catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
}
