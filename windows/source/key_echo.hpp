#pragma once
// The diagnostic key echo (sana 36424/36438): while an ordinary recording runs, each keyboard edge the input tick
// samples is appended to <result>\keys-seen.tsv as "<row>\t<key>\tdown|up" (row: the capture row of that tick), so a
// runner can hold a posted key until the game has seen it (windows/tests/perf/observed_keys.py). Record-only and
// bounded: one handle opened at arm (CREATE_NEW, shared for reading), one plain WriteFile per tick that has an edge,
// no flush in the tick. A failure is sticky and visible (the shim logs it once): nothing more is written, and the
// runner's wait times out. The capture and its seal stay the evidence; this file is diagnostic only.
#include <windows.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
namespace outrun::desktop {

struct EchoKeys { bool enter{},esc{},up{},down{},left{},right{}; };   // KeyboardProvenance's keys, in its order

class KeyEcho {
public:
    KeyEcho()=default;
    KeyEcho(const KeyEcho&)=delete;KeyEcho& operator=(const KeyEcho&)=delete;
    ~KeyEcho(){close();}
    // false (and failed(), error()) when the file cannot be created; an existing file is refused, never appended to.
    bool open(const std::filesystem::path& file){
        close();failed_=false;error_=0;lines_=0;prev_={};
        h_=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h_==INVALID_HANDLE_VALUE){h_=nullptr;failed_=true;error_=GetLastError();return false;}
        return true;
    }
    // Tests: write to this handle instead (a read-only one makes every write fail).
    void adopt(HANDLE h){close();failed_=false;error_=0;lines_=0;prev_={};h_=h;}
    bool is_open()const{return h_!=nullptr;}
    // One input tick's sampled keys: a line for each key whose state changed since the previous tick (all up before
    // the first). true while healthy; false on the tick a write fails (and after: the echo is closed and stays failed).
    bool tick(std::uint64_t row,const EchoKeys& now){
        if(!h_)return !failed_;
        static constexpr const char* names[6]{"enter","esc","up","down","left","right"};
        const std::array<bool,6> a{now.enter,now.esc,now.up,now.down,now.left,now.right};
        const std::array<bool,6> b{prev_.enter,prev_.esc,prev_.up,prev_.down,prev_.left,prev_.right};
        std::string text;
        for(std::size_t i=0;i<6;++i)if(a[i]!=b[i])text+=std::to_string(row)+"\t"+names[i]+(a[i]?"\tdown\n":"\tup\n");
        prev_=now;
        if(text.empty())return true;
        DWORD n=0;
        if(!WriteFile(h_,text.data(),DWORD(text.size()),&n,nullptr)||n!=text.size()){
            error_=GetLastError();if(!error_)error_=ERROR_WRITE_FAULT;failed_=true;close();return false;}
        for(const char c:text)if(c=='\n')++lines_;
        return true;
    }
    void close(){if(h_){CloseHandle(h_);h_=nullptr;}}
    bool failed()const{return failed_;}
    DWORD error()const{return error_;}
    std::uint64_t lines()const{return lines_;}
private:
    HANDLE h_{};bool failed_{};DWORD error_{};std::uint64_t lines_{};EchoKeys prev_{};
};

}
