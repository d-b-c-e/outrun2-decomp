#pragma once
// Fake DirectInput 8 (wide) COM objects for the provider's tests (INPUT-SHIM.md slice 3), in the manner of the toolkit's
// native/wheelffb/test/injection_test.cpp: scripted listings, object tables by DIJOYSTATE2 offset, range behavior,
// the identity GetDeviceInfo returns, Poll / GetDeviceState / Acquire results, delays, and misuse detection (a call
// after the final Release, a Release while GetDeviceState runs, any force-feedback call).
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wmicrosoft-exception-spec"   // the SDK's interfaces vs these test overrides
#endif
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace fakecom {
inline GUID make_guid(std::uint32_t d1,std::uint16_t d2){GUID g{};g.Data1=d1;g.Data2=d2;g.Data3=0x11f1;g.Data4[0]=0x80;g.Data4[7]=0x42;return g;}
constexpr DWORD AxisOffsets[8]{DIJOFS_X,DIJOFS_Y,DIJOFS_Z,DIJOFS_RX,DIJOFS_RY,DIJOFS_RZ,DIJOFS_SLIDER(0),DIJOFS_SLIDER(1)};

// P21k thread tails (Astra 13545; opt-in): an armed call registers exit work on the calling thread and returns at once,
// as a driver's thread-exit callback would. The thread's function can then report done while its thread still runs
// that work at exit: `ms` (a bounded tail) or, when negative, on `release` for ever.
struct ThreadTail {
    int ms=0;HANDLE release=nullptr;bool armed=false;
    ~ThreadTail(){if(!armed)return;if(ms>=0)Sleep(DWORD(ms));else WaitForSingleObject(release,INFINITE);}
};
inline void add_thread_tail(int ms,HANDLE release){static thread_local ThreadTail tail;tail.ms=ms;tail.release=release;tail.armed=true;}

struct DeviceScript {
    GUID instance{},product{};
    std::wstring name=L"Fake Wheel";
    GUID opened_instance{},opened_product{};      // what GetDeviceInfo returns (set equal unless a test changes it)
    bool axis[8]{true,false,true,false,false,true,false,false};   // X, Z, Rz
    LONG native_min[8]{},native_max[8]{};          // the range before (or without) the request
    bool accept_range=true;                        // SetProperty(DIPROP_RANGE) honored
    bool range_unreadable[8]{};                    // GetProperty(DIPROP_RANGE) fails for this axis
    int buttons=12,povs=1;
    bool fail_data_format=false,fail_device_info=false;
    std::wstring interface_path;                 // DIPROP_GUIDANDPATH read from this device (empty is valid metadata absence)
    HRESULT path_result=DI_OK;
    bool path_unterminated=false;
    DIJOYSTATE2 state{};
    std::deque<std::pair<HRESULT,HRESULT>> reads;  // the next (Poll, GetDeviceState) results; empty: (S_OK, S_OK)
    std::deque<HRESULT> acquires;                  // the next Acquire results; empty: S_OK
    bool fill_on_failure=false;                    // GetDeviceState writes the state even when it returns a failure
    bool pending_first_read=false;                 // each new device object's first GetDeviceState returns E_PENDING
    int state_delay_ms=0;
    // P21k (opt-in, off by default): one Acquire after `acquire_armed` is set, normally the worker's reacquisition, stays
    // inside the "driver": it signals `acquire_entered`, then waits `acquire_delay_ms` (a bounded slow call) or, when the
    // delay is negative, on `acquire_release` for ever (an event-backed call that never returns unless the test sets it).
    std::shared_ptr<std::atomic<bool>> acquire_armed;
    int acquire_delay_ms=0;
    HANDLE acquire_entered{},acquire_release{};
    // P21k release boundary (Astra 13277; opt-in, off by default): one Unacquire after `unacquire_armed` is set, normally
    // the provider's shutdown releasing an idle device, stays inside the "driver" the same way: `unacquire_entered`, then
    // `unacquire_delay_ms`, or for ever on `unacquire_release` when the delay is negative.
    std::shared_ptr<std::atomic<bool>> unacquire_armed;
    int unacquire_delay_ms=0;
    HANDLE unacquire_entered{},unacquire_release{};
    // Thread tails: the next Acquire (the worker's reacquisition) or Unacquire (the shutdown's release) after its flag is
    // set adds a tail of `tail_ms` (or for ever on `tail_release`) to the calling thread, signals `tail_entered`, returns.
    std::shared_ptr<std::atomic<bool>> acquire_tail_armed,unacquire_tail_armed;
    int tail_ms=0;
    HANDLE tail_entered{},tail_release{};
    DeviceScript(){for(int a=0;a<8;++a){native_min[a]=0;native_max[a]=65535;}std::memset(&state,0,sizeof state);for(auto& p:state.rgdwPOV)p=0xFFFFFFFFu;}
};

struct Counters {
    std::atomic<int> creates{0},releases{0},polls{0},state_reads{0},acquires{0},force_calls{0},after_release{0},release_during_read{0},path_reads{0};
};

class Device final:public IDirectInputDevice8W {
public:
    Device(DeviceScript& s,Counters& c,std::mutex& m):s_(s),c_(c),m_(m){}
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ULONG(++refs_);}
    ULONG STDMETHODCALLTYPE Release()override{
        if(in_read_.load()||in_call_.load()>0)++c_.release_during_read;   // a read or an Acquire still inside this object
        const int r=--refs_;
        if(r==0){released_=true;++c_.releases;}
        else if(r<0)++c_.after_release;
        return ULONG(r<0?0:r);
    }
    // The device
    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS)override{return check(),E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKW,LPVOID,DWORD)override{return check(),E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID p,LPDIPROPHEADER h)override{
        check();
        if(&p==&DIPROP_GUIDANDPATH){
            ++c_.path_reads;
            if(h->dwSize!=sizeof(DIPROPGUIDANDPATH)||h->dwHeaderSize!=sizeof(DIPROPHEADER)||h->dwHow!=DIPH_DEVICE||h->dwObj!=0)return DIERR_INVALIDPARAM;
            auto* value=reinterpret_cast<DIPROPGUIDANDPATH*>(h);
            if(s_.path_unterminated)for(auto& c:value->wszPath)c=L'x';
            else wcsncpy_s(value->wszPath,s_.interface_path.c_str(),_TRUNCATE);
            return s_.path_result;   // deliberately writes even on failure, like a faulty driver
        }
        if(&p!=&DIPROP_RANGE)return E_NOTIMPL;   // DIPROP_* are pseudo-GUID addresses: compare addresses only
        if(h->dwHow!=DIPH_BYOFFSET)return DIERR_INVALIDPARAM;
        for(int a=0;a<8;++a)if(AxisOffsets[a]==h->dwObj&&s_.axis[a]){
            if(s_.range_unreadable[a])return DIERR_UNSUPPORTED;
            auto* r=reinterpret_cast<DIPROPRANGE*>(h);
            r->lMin=ranged_?0:s_.native_min[a];r->lMax=ranged_?65535:s_.native_max[a];
            return DI_OK;
        }
        return DIERR_OBJECTNOTFOUND;
    }
    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID p,LPCDIPROPHEADER)override{
        check();
        if(&p==&DIPROP_RANGE){if(!s_.accept_range)return DIERR_UNSUPPORTED;ranged_=true;}
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE Acquire()override{
        check();++c_.acquires;
        if(s_.acquire_tail_armed&&s_.acquire_tail_armed->exchange(false)){add_thread_tail(s_.tail_ms,s_.tail_release);if(s_.tail_entered)SetEvent(s_.tail_entered);}
        if(s_.acquire_armed&&s_.acquire_armed->exchange(false)){
            ++in_call_;
            if(s_.acquire_entered)SetEvent(s_.acquire_entered);
            if(s_.acquire_delay_ms>=0)std::this_thread::sleep_for(std::chrono::milliseconds(s_.acquire_delay_ms));
            else WaitForSingleObject(s_.acquire_release,INFINITE);
            --in_call_;
            check();   // a Release while it waited would show here as a call after release
        }
        std::lock_guard<std::mutex> l(m_);
        if(s_.acquires.empty())return DI_OK;
        const auto hr=s_.acquires.front();s_.acquires.pop_front();return hr;
    }
    HRESULT STDMETHODCALLTYPE Unacquire()override{
        check();
        if(s_.unacquire_tail_armed&&s_.unacquire_tail_armed->exchange(false)){add_thread_tail(s_.tail_ms,s_.tail_release);if(s_.tail_entered)SetEvent(s_.tail_entered);}
        if(s_.unacquire_armed&&s_.unacquire_armed->exchange(false)){
            ++in_call_;
            if(s_.unacquire_entered)SetEvent(s_.unacquire_entered);
            if(s_.unacquire_delay_ms>=0)std::this_thread::sleep_for(std::chrono::milliseconds(s_.unacquire_delay_ms));
            else WaitForSingleObject(s_.unacquire_release,INFINITE);
            --in_call_;
            check();
        }
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD size,LPVOID data)override{
        check();++c_.state_reads;
        in_read_=true;
        if(s_.state_delay_ms)std::this_thread::sleep_for(std::chrono::milliseconds(s_.state_delay_ms));
        HRESULT hr=DI_OK;
        {std::lock_guard<std::mutex> l(m_);hr=pending_state_;pending_state_=DI_OK;
         if(s_.pending_first_read&&!first_read_done_){first_read_done_=true;hr=E_PENDING;}
         if(SUCCEEDED(hr)||s_.fill_on_failure){if(size==sizeof(DIJOYSTATE2))std::memcpy(data,&s_.state,size);}}
        in_read_=false;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD)override{return check(),E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT)override{check();return s_.fail_data_format?DIERR_INVALIDPARAM:DI_OK;}
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE)override{return check(),E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND,DWORD)override{check();return DI_OK;}
    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEW,DWORD obj,DWORD how)override{
        check();
        if(how!=DIPH_BYOFFSET)return DIERR_INVALIDPARAM;
        for(int a=0;a<8;++a)if(AxisOffsets[a]==obj)return s_.axis[a]?DI_OK:DIERR_OBJECTNOTFOUND;
        for(int b=0;b<128;++b)if(DWORD(DIJOFS_BUTTON(b))==obj)return b<s_.buttons?DI_OK:DIERR_OBJECTNOTFOUND;
        for(int p=0;p<4;++p)if(DWORD(DIJOFS_POV(p))==obj)return p<s_.povs?DI_OK:DIERR_OBJECTNOTFOUND;
        return DIERR_OBJECTNOTFOUND;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEW di)override{
        check();
        if(s_.fail_device_info)return E_FAIL;
        di->guidInstance=s_.opened_instance;di->guidProduct=s_.opened_product;
        wcsncpy_s(di->tszProductName,s_.name.c_str(),_TRUNCATE);
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND,DWORD)override{return check(),E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD,REFGUID)override{return check(),E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID,LPCDIEFFECT,LPDIRECTINPUTEFFECT*,LPUNKNOWN)override{++c_.force_calls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKW,LPVOID,DWORD)override{++c_.force_calls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOW,REFGUID)override{++c_.force_calls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD)override{++c_.force_calls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD)override{++c_.force_calls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK,LPVOID,DWORD)override{++c_.force_calls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE)override{++c_.force_calls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Poll()override{
        check();++c_.polls;
        std::lock_guard<std::mutex> l(m_);
        HRESULT p=DI_OK;pending_state_=DI_OK;
        if(!s_.reads.empty()){p=s_.reads.front().first;pending_state_=s_.reads.front().second;s_.reads.pop_front();}
        return p;
    }
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD,LPCDIDEVICEOBJECTDATA,LPDWORD,DWORD)override{++c_.force_calls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCWSTR,LPDIENUMEFFECTSINFILECALLBACK,LPVOID,DWORD)override{++c_.force_calls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCWSTR,DWORD,LPDIFILEEFFECT,DWORD)override{++c_.force_calls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATW,LPCWSTR,DWORD)override{return check(),E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATW,LPCWSTR,DWORD)override{return check(),E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERW)override{return check(),E_NOTIMPL;}
    bool released()const{return released_.load();}
private:
    DeviceScript& s_;Counters& c_;std::mutex& m_;
    std::atomic<int> refs_{1};
    std::atomic<bool> released_{false},in_read_{false};
    std::atomic<int> in_call_{0};
    bool ranged_=false,first_read_done_=false;
    HRESULT pending_state_=DI_OK;
    void check(){if(released_.load())++c_.after_release;}
};

class Input final:public IDirectInput8W {
public:
    std::mutex m;
    std::deque<DeviceScript> scripts;   // stable addresses: devices hold references
    std::vector<bool> attached;
    std::vector<Device*> made;
    Counters counters;
    int enumerate_delay_ms=0,create_delay_ms=0;
    // P21k (opt-in): one EnumDevices after `enumerate_armed` signals `enumerate_entered` and waits on `enumerate_release`
    // for ever (a listing that never returns) unless the test sets it.
    std::shared_ptr<std::atomic<bool>> enumerate_armed;
    HANDLE enumerate_entered{},enumerate_release{};
    // P21k release boundary (opt-in): one Release after `release_armed` is set, normally the provider's final release of
    // DirectInput, signals `release_entered` and waits `release_delay_ms`, or for ever on `release_release` when negative.
    std::shared_ptr<std::atomic<bool>> release_armed;
    int release_delay_ms=0;
    HANDLE release_entered{},release_release{};
    std::atomic<int> refs{1},enumerations{0};
    // Add every device before the first open: devices keep references to their scripts (a std::deque would also do).
    std::size_t add(DeviceScript s){s.opened_instance=s.instance;s.opened_product=s.product;scripts.push_back(s);attached.push_back(true);return scripts.size()-1;}
    ~Input(){for(auto* d:made)delete d;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ULONG(++refs);}
    ULONG STDMETHODCALLTYPE Release()override{
        if(release_armed&&release_armed->exchange(false)){
            if(release_entered)SetEvent(release_entered);
            if(release_delay_ms>=0)std::this_thread::sleep_for(std::chrono::milliseconds(release_delay_ms));
            else WaitForSingleObject(release_release,INFINITE);
        }
        return ULONG(--refs);
    }
    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID g,LPDIRECTINPUTDEVICE8W* out,LPUNKNOWN)override{
        if(create_delay_ms)std::this_thread::sleep_for(std::chrono::milliseconds(create_delay_ms));
        std::lock_guard<std::mutex> l(m);
        for(std::size_t i=0;i<scripts.size();++i)if(attached[i]&&IsEqualGUID(g,scripts[i].instance)){
            auto* d=new Device(scripts[i],counters,m);made.push_back(d);++counters.creates;*out=d;return DI_OK;}
        *out=nullptr;return DIERR_DEVICENOTREG;
    }
    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD,LPDIENUMDEVICESCALLBACKW cb,LPVOID user,DWORD)override{
        if(enumerate_delay_ms)std::this_thread::sleep_for(std::chrono::milliseconds(enumerate_delay_ms));
        if(enumerate_armed&&enumerate_armed->exchange(false)){
            if(enumerate_entered)SetEvent(enumerate_entered);
            WaitForSingleObject(enumerate_release,INFINITE);
        }
        std::vector<DIDEVICEINSTANCEW> list;
        {std::lock_guard<std::mutex> l(m);++enumerations;
         for(std::size_t i=0;i<scripts.size();++i)if(attached[i]){
             DIDEVICEINSTANCEW d{};d.dwSize=sizeof d;d.guidInstance=scripts[i].instance;d.guidProduct=scripts[i].product;
             d.dwDevType=DI8DEVTYPE_DRIVING;wcsncpy_s(d.tszProductName,scripts[i].name.c_str(),_TRUNCATE);list.push_back(d);}}
        for(const auto& d:list)if(cb(&d,user)!=DIENUM_CONTINUE)break;
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND,DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID,LPCWSTR,LPGUID)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCWSTR,LPDIACTIONFORMATW,LPDIENUMDEVICESBYSEMANTICSCBW,LPVOID,DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK,LPDICONFIGUREDEVICESPARAMSW,DWORD,LPVOID)override{return E_NOTIMPL;}
};
}
