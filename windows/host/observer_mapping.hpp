#pragma once
// Windows x64 named mapping wrapper for the atomic transport. Mapping existence
// is not readiness: readers accept only a published even nonzero sequence.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "observer_transport.hpp"
#include <new>
#include <stdexcept>
#include <string>

namespace dbce::shim::observe {
class Mapping {
    HANDLE handle_{};
    void* view_{};
    bool writer_{};
public:
    Mapping(const std::wstring& name,bool create) : writer_(create) {
        if(name.empty() || name.size()>200 || name.rfind(L"Local\\DbceShimObserver-",0)!=0)
            throw std::invalid_argument("Observer name must use its local namespace");
        if(create) {
            handle_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,DWORD(sizeof(Storage)),name.c_str());
            const DWORD error=GetLastError();
            if(handle_ && error==ERROR_ALREADY_EXISTS) {CloseHandle(handle_);handle_=nullptr;throw std::runtime_error("Observer mapping already exists");}
        }else handle_=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());
        if(!handle_) throw std::runtime_error("Observer mapping open failed");
        view_=MapViewOfFile(handle_,create?FILE_MAP_ALL_ACCESS:FILE_MAP_READ,0,0,sizeof(Storage));
        if(!view_) {CloseHandle(handle_);handle_=nullptr;throw std::runtime_error("Observer map failed");}
        if(create) new(view_) Storage();
    }
    Mapping(const Mapping&)=delete;
    Mapping& operator=(const Mapping&)=delete;
    ~Mapping() {if(view_) UnmapViewOfFile(view_);if(handle_) CloseHandle(handle_);}
    Storage& writable() {if(!writer_) throw std::logic_error("Read-only observer");return *static_cast<Storage*>(view_);}
    const Storage& storage() const {return *static_cast<const Storage*>(view_);}
};
}
