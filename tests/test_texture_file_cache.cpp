// TextureFileCache (src/platform/texture_file_cache.hpp): banks queued one after another are all read ahead by the one
// worker, in order, and ready() holds until the last is read; the game's thread then reads them from memory (Wheelkit
// issue outrun-2006-0012: a stage's end queues several banks at once, and only the first was read ahead).
#include "platform/texture_file_cache.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
using outrun::platform::TextureFileCache;
static int checks=0,failures=0;
static void check(bool ok,const char* what){++checks;if(!ok){++failures;std::printf("FAIL %s\n",what);}}
int main(){
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/"or2_texture_file_cache_test";
    std::error_code e;fs::remove_all(root,e);fs::create_directories(root/"a");fs::create_directories(root/"b");
    std::vector<fs::path> a,b;
    for(int k=0;k<40;++k)for(auto* list:{&a,&b}){
        const auto path=root/(list==&a?"a":"b")/(std::to_string(k)+".dds");
        std::ofstream f(path,std::ios::binary);const std::string bytes(4096+std::size_t(k),char('A'+k%26));f.write(bytes.data(),std::streamsize(bytes.size()));
        list->push_back(path);
    }
    {   TextureFileCache cache(64u<<20);
        check(cache.ready(),"nothing queued: ready");
        cache.prefetch(a);cache.prefetch(b);   // the second while the first is still being read
        for(int k=0;k<2000&&!cache.ready();++k)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(cache.ready(),"both banks read ahead");
        const auto s=cache.stats();
        check(s.prefetched==80u&&s.reads==80u&&s.entries==80u,"every file of both banks is in memory");
        std::vector<std::uint8_t> out;
        check(cache.read(b.back(),out)&&out.size()==4096u+39u&&out[0]=='A'+39%26,"a file of the second bank comes from memory");
        check(cache.stats().hits==1u&&cache.stats().reads==80u,"no read on the caller's thread");
        cache.prefetch(a);
        for(int k=0;k<2000&&!cache.ready();++k)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(cache.ready()&&cache.stats().reads==80u,"a bank read again is found in memory");
    }
    {   TextureFileCache small(40u*4200u);   // room for one bank: the oldest files go first
        small.prefetch(a);small.prefetch(b);
        for(int k=0;k<2000&&!small.ready();++k)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(small.ready()&&small.stats().evicted>0u&&small.stats().bytes<=40u*4200u,"the capacity holds");
    }
    {   TextureFileCache gone(64u<<20);gone.prefetch(a);gone.prefetch(b);}   // destroyed while reading: joins, no crash
    fs::remove_all(root,e);
    std::printf("%s: texture file cache: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
