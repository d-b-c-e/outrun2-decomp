#pragma once
// Bounded, read-only command snapshots. Call from the control worker only.
// Bounds are bytes, not I/O latency: the host still owns a deadline and retains
// this worker's state if a filesystem/filter driver blocks a call.
#include <windows.h>
#include "control_pump.hpp"

namespace dbce::shim::control {
class SnapshotFile {
    HANDLE file_{INVALID_HANDLE_VALUE};
    std::size_t size_{};
    Read result_;
    bool taken_{};
    void error(const char* what,DWORD code=0){
        result_={ReadState::Error,{},std::string(what)+(code?" (Win32 "+std::to_string(code)+")":"")};
    }
public:
    // Parent directories must already belong to the verified entry fence. This
    // helper does not create paths or follow a reparse point at the leaf. It
    // rejects alternate streams; it is not a hostile-process security boundary.
    SnapshotFile(const std::wstring& path,std::size_t bound){
        if(bound==0||bound>MaxBatchBytes||path.empty()||path.find(L':',2)!=std::wstring::npos){error("invalid snapshot path or bound");return;}
        file_=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if(file_==INVALID_HANDLE_VALUE){
            const DWORD e=GetLastError();
            if(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND)result_={ReadState::Missing,{},{}};
            // Another handle holds the file incompatibly (a writer, or a publisher mid-replace). Nothing is read, and
            // the pump decides whether to wait (Busy, bounded) or fail.
            else if(e==ERROR_SHARING_VIOLATION||e==ERROR_LOCK_VIOLATION)result_={ReadState::Busy,{},"snapshot open busy (Win32 "+std::to_string(e)+")"};
            else error("snapshot open refused",e);
            return;
        }
        if(GetFileType(file_)!=FILE_TYPE_DISK){error("snapshot is not a disk file");return;}
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(file_,&info)){error("snapshot metadata unavailable",GetLastError());return;}
        if(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){error("snapshot is a directory or reparse point");return;}
        if(info.nFileSizeHigh||info.nFileSizeLow>bound){error("snapshot exceeds byte bound");return;}
        size_=info.nFileSizeLow;result_.state=ReadState::Bytes;
    }
    SnapshotFile(const SnapshotFile&)=delete;SnapshotFile& operator=(const SnapshotFile&)=delete;
    ~SnapshotFile(){if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_);}
    Read take(){
        if(taken_)return {ReadState::Error,{},"snapshot already consumed"};taken_=true;
        if(result_.state!=ReadState::Bytes)return result_;
        std::string bytes(size_,'\0');std::size_t done=0;
        while(done<size_){
            DWORD read=0;
            if(!ReadFile(file_,bytes.data()+done,DWORD(size_-done),&read,nullptr)){error("snapshot read failed",GetLastError());return result_;}
            if(!read){error("snapshot ended before its declared size");return result_;}
            done+=read;
        }
        // No FILE_SHARE_WRITE: an existing writer makes open fail, and no new
        // writer can alter this object while held. Atomic replacement is allowed
        // (FILE_SHARE_DELETE), but this handle still reads the original object.
        char extra=0;DWORD count=0;
        if(!ReadFile(file_,&extra,1,&count,nullptr)){error("snapshot end check failed",GetLastError());return result_;}
        if(count){error("snapshot grew during read");return result_;}
        result_.bytes=std::move(bytes);return result_;
    }
};
inline Read read_snapshot(const std::wstring& path,std::size_t bound){return SnapshotFile(path,bound).take();}
}
