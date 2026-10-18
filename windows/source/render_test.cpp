// The opt-in rendering test of a latched process (render_test.hpp): the request's parse and the fence step. The capture
// start is render_test_capture.cpp (it needs the desktop's input filter).
#include "render_test.hpp"
#include "test_fence.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fstream>
#include <vector>
namespace outrun::desktop {
namespace {
bool number(const std::string& v,unsigned max,unsigned& out){
    if(v.empty()||v.size()>6)return false;
    unsigned n=0;
    for(const char c:v){if(c<'0'||c>'9')return false;n=n*10u+unsigned(c-'0');}
    if(n>max)return false;
    out=n;return true;
}
}

bool parse_render_test(const std::string& bytes,const std::string& nonce,RenderTest& out,std::string& why){
    out=RenderTest{};
    if(nonce.empty()){why="no session nonce (inject.on did not parse)";return false;}
    if(bytes.size()>1024){why="larger than 1 KiB";return false;}
    std::vector<std::string> lines;
    std::string line;
    for(const char c:bytes){
        if(c=='\n'){if(!line.empty()&&line.back()=='\r')line.pop_back();lines.push_back(line);line.clear();continue;}
        if(static_cast<unsigned char>(c)<0x20&&c!='\r'){why="a control character";return false;}
        if(static_cast<unsigned char>(c)>0x7e){why="not ASCII";return false;}
        line+=c;
    }
    if(!line.empty())lines.push_back(line);
    if(lines.empty()||lines[0]!=RenderTestSchema){why=std::string("the first line is not ")+RenderTestSchema;return false;}
    RenderTest t;
    bool seen_nonce=false,seen_screens=false,seen_views=false,seen_frames=false,seen_every=false,seen_after=false,seen_scale=false;
    for(std::size_t i=1;i<lines.size();++i){
        const auto& l=lines[i];
        const auto eq=l.find('=');
        if(eq==std::string::npos||eq==0){why="line "+std::to_string(i+1)+" is not key=value";return false;}
        const std::string key=l.substr(0,eq),value=l.substr(eq+1);
        const auto once=[&](bool& seen){if(seen){why="key "+key+" given twice";return false;}seen=true;return true;};
        if(key=="nonce"){if(!once(seen_nonce))return false;if(value!=nonce){why="the nonce is not this session's";return false;}}
        else if(key=="screens"){if(!once(seen_screens))return false;
            if(value=="off")t.screens=TripleScreens::off;else if(value=="surround")t.screens=TripleScreens::surround;
            else if(value=="separate")t.screens=TripleScreens::separate;else{why="screens is off, surround or separate";return false;}}
        else if(key=="views"){if(!once(seen_views))return false;
            if(value=="planar")t.views=TripleProjection::planar;else if(value=="angled")t.views=TripleProjection::angled;
            else{why="views is planar or angled";return false;}}
        else if(key=="frames"){if(!once(seen_frames))return false;if(!number(value,300u,t.frames)){why="frames is 0..300";return false;}}
        else if(key=="every"){if(!once(seen_every))return false;if(!number(value,60u,t.every)||t.every<1u){why="every is 1..60";return false;}}
        else if(key=="after"){if(!once(seen_after))return false;if(!number(value,36000u,t.after)){why="after is 0..36000";return false;}}
        else if(key=="scale"){if(!once(seen_scale))return false;
            if(!number(value,4u,t.scale)||(t.scale!=1u&&t.scale!=2u&&t.scale!=4u)){why="scale is 1, 2 or 4";return false;}}
        else{why="unknown key "+key.substr(0,24);return false;}
    }
    if(!seen_nonce){why="no nonce";return false;}
    if(t.views==TripleProjection::angled&&t.screens==TripleScreens::off){why="views=angled needs screens surround or separate";return false;}
    t.requested=true;out=t;
    return true;
}

bool load_render_test(const std::wstring& dbce_folder,const std::wstring& run_root,const std::string& nonce,RenderTest& out,
                      std::string& why){
    out=RenderTest{};
    const std::wstring path=dbce_folder+L"\\render-test.txt";
    const DWORD a=GetFileAttributesW(path.c_str());
    if(a==INVALID_FILE_ATTRIBUTES){
        if(GetLastError()==ERROR_FILE_NOT_FOUND)return true;   // absent: no rendering test
        why="cannot inspect render-test.txt (error "+std::to_string(GetLastError())+")";return false;
    }
    if(a&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)||fence::has_reparse_point_on_path(path)){
        why="render-test.txt is not a plain file";return false;}
    std::string bytes;
    {   std::ifstream f(path,std::ios::binary);
        if(!f){why="cannot read render-test.txt";return false;}
        bytes.assign(1025,'\0');f.read(bytes.data(),std::streamsize(bytes.size()));bytes.resize(std::size_t(f.gcount()));}
    if(!parse_render_test(bytes,nonce,out,why))return false;
    const HANDLE h=CreateFileW((run_root+L"\\render-test.txt").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE){why="cannot keep render-test.txt in the run folder";out=RenderTest{};return false;}
    DWORD written=0;
    const bool ok=WriteFile(h,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size();
    CloseHandle(h);
    if(!ok){why="cannot keep render-test.txt in the run folder";out=RenderTest{};return false;}
    if(out.frames>0u&&!CreateDirectoryW((run_root+L"\\frames").c_str(),nullptr)){
        why="cannot make the run's frames folder";out=RenderTest{};return false;}
    return true;
}
}
