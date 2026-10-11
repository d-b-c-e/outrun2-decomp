// P30 discovery only. Enumerates attached controller identities; never calls CreateDevice or acquires a device.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
struct Row { GUID instance,product;std::wstring name; };
struct Rows { std::vector<Row> values;bool failed=false; };
static BOOL CALLBACK collect(const DIDEVICEINSTANCEW* d,void* context){
    auto& rows=*static_cast<Rows*>(context);
    try{
        if(rows.values.size()>=256){rows.failed=true;return DIENUM_STOP;}
        rows.values.push_back({d->guidInstance,d->guidProduct,d->tszProductName});return DIENUM_CONTINUE;
    }catch(...){rows.failed=true;return DIENUM_STOP;}
}
static std::string guid(const GUID& g){
    char out[40];std::snprintf(out,sizeof out,"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",g.Data1,g.Data2,g.Data3,
        g.Data4[0],g.Data4[1],g.Data4[2],g.Data4[3],g.Data4[4],g.Data4[5],g.Data4[6],g.Data4[7]);return out;
}
int wmain(int argc,wchar_t** argv){
    try{
        if(argc!=3||std::wstring(argv[1])!=L"--list-only")throw std::runtime_error("usage: dinput_inventory --list-only <new folder>");
        const std::filesystem::path result(argv[2]);
        if(!std::filesystem::create_directory(result))throw std::runtime_error("result folder must be new");
        IDirectInput8W* input=nullptr;
        if(FAILED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&input),nullptr))||!input)
            throw std::runtime_error("DirectInput8Create failed");
        Rows rows;const auto hr=input->EnumDevices(DI8DEVCLASS_GAMECTRL,collect,&rows,DIEDFL_ATTACHEDONLY);
        input->Release();input=nullptr;
        if(FAILED(hr)||rows.failed)throw std::runtime_error("attached identity enumeration failed or exceeded its bound");
        std::ofstream file(result/L"dinput.tsv",std::ios::binary);file<<"instance\tproduct\tname\n";
        for(const auto& row:rows.values){
            const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,row.name.data(),int(row.name.size()),nullptr,0,nullptr,nullptr);
            if(size<=0)throw std::runtime_error("device name could not be encoded");
            std::string name(std::size_t(size),'\0');
            if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,row.name.data(),int(row.name.size()),name.data(),size,nullptr,nullptr)!=size)throw std::runtime_error("device name changed during encoding");
            for(auto& c:name)if(c=='\t'||c=='\r'||c=='\n')c=' ';
            file<<guid(row.instance)<<'\t'<<guid(row.product)<<'\t'<<name<<'\n';
        }
        file.close();if(!file.good())throw std::runtime_error("inventory output failed");
        std::cout<<"PASS: "<<rows.values.size()<<" attached DirectInput identities; no device opened.\n";return 0;
    }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
}
