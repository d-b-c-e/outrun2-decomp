// The diagnostic key echo (source/key_echo.hpp; sana 36438): edges only, in key order, at the given row; a reader can
// follow the file while it is written; an existing file is refused; a failed write is sticky and stops the echo.
#include "key_echo.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
using namespace outrun::desktop;
namespace {
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v){std::fprintf(stderr,"key echo: FAIL: %s\n",what.c_str());std::exit(1);}}
std::string read_shared(const std::filesystem::path& p){   // as the runner reads it: while the writer holds the file
    HANDLE h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return "<unreadable "+std::to_string(GetLastError())+">";
    std::string s;char buf[4096];DWORD n=0;
    while(ReadFile(h,buf,sizeof buf,&n,nullptr)&&n)s.append(buf,n);
    CloseHandle(h);return s;
}
}
int main(){
    wchar_t tmp[MAX_PATH];GetTempPathW(MAX_PATH,tmp);
    const auto dir=std::filesystem::path(tmp)/(L"key-echo-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(dir);
    const auto file=dir/L"keys-seen.tsv";
    {
        KeyEcho e;
        check(e.tick(0,{true})&&!e.is_open(),"an echo never opened writes nothing and is not a failure");
        check(e.open(file)&&e.is_open(),"opened");
        check(read_shared(file).empty(),"nothing before the first edge");
        check(e.tick(5,{}),"no edge: no line");
        EchoKeys k;k.enter=true;
        check(e.tick(6,k)&&read_shared(file)=="6\tenter\tdown\n","a press, readable while the writer holds the file");
        check(e.tick(7,k)&&e.tick(8,k)&&read_shared(file)=="6\tenter\tdown\n","held: no more lines");
        k.enter=false;k.up=true;k.left=true;
        check(e.tick(9,k),"three edges in one tick");
        check(read_shared(file)=="6\tenter\tdown\n9\tenter\tup\n9\tup\tdown\n9\tleft\tdown\n","in key order, at the tick's row");
        k={};k.esc=true;k.down=true;k.right=true;
        check(e.tick(10,k)&&e.lines()==9,"every edge counted: 1 + 3 + 5 lines");
        const auto text=read_shared(file);
        check(text.find("10\tesc\tdown\n")!=std::string::npos&&text.find("10\tup\tup\n")!=std::string::npos&&text.find("10\tleft\tup\n")!=std::string::npos&&
              text.find("10\tdown\tdown\n")!=std::string::npos&&text.find("10\tright\tdown\n")!=std::string::npos,"esc, down and right, and the releases");
        KeyEcho twice;
        check(!twice.open(file)&&twice.failed()&&twice.error()==ERROR_FILE_EXISTS,"an existing file is refused, never appended to");
        e.close();
        check(e.tick(11,{})&&!e.failed(),"after close: nothing written, not a failure");
        check(read_shared(file)==text,"the file is unchanged after close");
    }
    {   // A failed write (a read-only handle): reported on that tick, sticky, closed, nothing written after.
        HANDLE ro=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(ro!=INVALID_HANDLE_VALUE,"a read-only handle");
        KeyEcho e;e.adopt(ro);
        check(e.tick(1,{}),"no edge: no write, still healthy");
        EchoKeys k;k.up=true;
        check(!e.tick(2,k)&&e.failed()&&e.error()!=0&&!e.is_open(),"the failing write is reported and closes the echo");
        k.up=false;
        check(!e.tick(3,k)&&e.failed(),"it stays failed");
    }
    std::error_code ec;std::filesystem::remove_all(dir,ec);
    std::printf("key echo: %d checks passed\n",checks);
    return 0;
}
