#pragma once
// Whole-file command publication outside the input/game thread. Parent directories
// must already be caller-owned/fenced, as for SnapshotFile. Not a hostile-process boundary.
#include <windows.h>
#include "shim.hpp"
#include <atomic>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace dbce::shim::control {
namespace publish_detail {
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle(){close();}
    void close(){if(value!=INVALID_HANDLE_VALUE){CloseHandle(value);value=INVALID_HANDLE_VALUE;}}
};
using SetInformation=BOOL(WINAPI*)(HANDLE,FILE_INFO_BY_HANDLE_CLASS,LPVOID,DWORD);
// The injectable syscall is only used by the unsupported-platform regression.
inline void publish(const std::wstring& path,std::string_view bytes,SetInformation set_information){
    if(path.empty()||path.find(L':',2)!=std::wstring::npos||bytes.size()>MaxBatchBytes)
        throw std::invalid_argument("invalid publication path or byte bound");
    const DWORD count=GetFullPathNameW(path.c_str(),0,nullptr,nullptr);
    if(!count)throw std::runtime_error("publication full path failed: "+std::to_string(GetLastError()));
    std::vector<wchar_t> full(count);
    const DWORD length=GetFullPathNameW(path.c_str(),count,full.data(),nullptr);
    if(!length||length>=count)throw std::runtime_error("publication full path changed");
    const std::wstring target(full.data(),length);
    static std::atomic<unsigned long long> sequence{};
    std::wstring temp;
    Handle file;
    for(int attempt=0;attempt<16;++attempt){
        temp=target+L".publish-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence)+L".tmp";
        file.value=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file.value!=INVALID_HANDLE_VALUE)break;
        if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)
            throw std::runtime_error("publication temp create failed: "+std::to_string(GetLastError()));
    }
    if(file.value==INVALID_HANDLE_VALUE)throw std::runtime_error("publication temp collisions");
    try{
        DWORD written=0;
        if(!WriteFile(file.value,bytes.data(),DWORD(bytes.size()),&written,nullptr)||written!=bytes.size())
            throw std::runtime_error("publication write failed: "+std::to_string(GetLastError()));
        file.close(); // A published object must never retain a write-access handle.
        file.value=CreateFileW(temp.c_str(),DELETE|SYNCHRONIZE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file.value==INVALID_HANDLE_VALUE)throw std::runtime_error("publication rename handle failed: "+std::to_string(GetLastError()));
        // Use the SDK layout (including native HANDLE alignment), not x64-only offsets.
        const auto name_bytes=DWORD(target.size()*sizeof(wchar_t));
        std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO)+name_bytes,0);
        auto* info=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
        info->Flags=FILE_RENAME_FLAG_REPLACE_IF_EXISTS|FILE_RENAME_FLAG_POSIX_SEMANTICS;
        info->FileNameLength=name_bytes;
        std::memcpy(info->FileName,target.data(),name_bytes);
        if(!set_information(file.value,FileRenameInfoEx,info,DWORD(storage.size())))
            throw std::runtime_error("publication FileRenameInfoEx failed: "+std::to_string(GetLastError()));
        // Existing SnapshotFile handles keep old bytes; new opens see this complete object.
        // There is no target open, truncate, delete, ReplaceFileW fallback or sharing retry.
    }catch(const std::exception& failure){
        const std::string first=failure.what();file.close();
        if(!DeleteFileW(temp.c_str())&&GetLastError()!=ERROR_FILE_NOT_FOUND)
            throw std::runtime_error(first+"; owned temp cleanup failed: "+std::to_string(GetLastError()));
        throw;
    }
}
}
inline void publish_snapshot(const std::wstring& path,std::string_view bytes){
    publish_detail::publish(path,bytes,&SetFileInformationByHandle);
}
}
