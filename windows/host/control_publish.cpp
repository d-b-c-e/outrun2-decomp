#include "control_publish.hpp"
#include <iostream>

// Standalone publisher for Python/other hosts; binary stdin, one fenced target.
int wmain(int argc,wchar_t** argv){
    try{
        if(argc!=2)throw std::runtime_error("usage: control_publish.exe <target> (complete bytes on stdin, <=4096)");
        std::string bytes;char buffer[1024];
        for(;;){
            DWORD count=0;
            if(!ReadFile(GetStdHandle(STD_INPUT_HANDLE),buffer,sizeof(buffer),&count,nullptr)){
                if(GetLastError()==ERROR_BROKEN_PIPE)break;
                throw std::runtime_error("publication stdin read failed: "+std::to_string(GetLastError()));
            }
            if(!count)break;
            bytes.append(buffer,count);
            if(bytes.size()>dbce::shim::MaxBatchBytes)throw std::runtime_error("publication exceeds 4096 bytes");
        }
        dbce::shim::control::publish_snapshot(argv[1],bytes);return 0;
    }catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}
}
