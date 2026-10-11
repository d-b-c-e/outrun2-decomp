#include "worker.hpp"
#include "session_writer.h"
#include "../../shim/sha256.hpp"
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace dbce::shim::recorder;
using namespace std::chrono_literals;
namespace fs=std::filesystem;
struct Gate {std::atomic<bool> entered{false},resume{false};};
struct FileIdentity {
    std::string nonce,sha;
    std::uint64_t bytes{};
    const std::thread::id controller=std::this_thread::get_id();
    FileIdentity(){
        std::array<std::uint8_t,16> random{};
        if(BCryptGenRandom(nullptr,random.data(),ULONG(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)throw std::runtime_error("capture nonce failed");
        nonce=dbce::shim::hex(random.data(),random.size());
    }
    void closed_source(const fs::path& path){
        if(std::this_thread::get_id()==controller)throw std::runtime_error("identity must be recorded by the worker");
        struct File {HANDLE h;~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}} file{
            CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
        LARGE_INTEGER size{};
        if(file.h==INVALID_HANDLE_VALUE||!GetFileSizeEx(file.h,&size)||size.QuadPart<=0||size.QuadPart>64*1024*1024)throw std::runtime_error("closed capture size unavailable");
        std::vector<std::uint8_t> data(std::size_t(size.QuadPart));DWORD count=0;
        if(!ReadFile(file.h,data.data(),DWORD(data.size()),&count,nullptr)||count!=data.size())throw std::runtime_error("closed capture hash read failed");
        sha=dbce::shim::hex(dbce::shim::sha256(data.data(),data.size()));bytes=data.size();
    }
};
struct NativeSink final:Sink {
    dbce::session::Writer writer;
    fs::path path;
    dbce::session::Limits limits;
    std::shared_ptr<Gate> write_gate,stop_gate;
    const std::shared_ptr<FileIdentity> identity;
    explicit NativeSink(fs::path p,std::shared_ptr<FileIdentity> id):path(std::move(p)),identity(std::move(id)){}
    bool open()override{
        dbce::session::Metadata m;m.game="outrun-recorder-lifecycle-fixture";m.pluginVersion="spike-only";m.toolkitVersion="writer-85147b8";
        m.properties["qualification"]="offline worker lifecycle, no game or device";
        m.properties["captureNonce"]=identity->nonce;
        return writer.open(path.wstring(),m,limits);
    }
    static void gate(const std::shared_ptr<Gate>& g){if(g){g->entered=true;while(!g->resume)std::this_thread::yield();}}
    bool write(const Packet& p)override{
        gate(write_gate);
        dbce::session::Channels channels;
        channels["source.seq.lo"]=std::uint32_t(p.sequence);channels["source.seq.hi"]=std::uint32_t(p.sequence>>32);
        channels["source.us.lo"]=std::uint32_t(p.source_us);channels["source.us.hi"]=std::uint32_t(std::uint64_t(p.source_us)>>32);
        for(std::uint32_t i=0;i<p.width;++i)channels["value."+std::to_string(i)]=p.values[i];
        return writer.sample(double(p.source_us)/1000000.0,channels);
    }
    bool stop()override{
        gate(stop_gate);
        if(!writer.stop())return false;
        if(writer.active())throw std::runtime_error("writer remained open after stop");
        identity->closed_source(path);return true;
    }
    bool stopped_at_limit()const override{
        const auto& r=writer.reason();
        return !writer.active()&&(r=="durationLimit"||r=="fileSizeLimit"||r=="recordSizeLimit");
    }
    void abandon()noexcept override{writer.abandon();}
    std::string reason()const override{return writer.reason();}
};
void await(std::atomic<bool>& signal){
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(!signal){if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("native fixture barrier timeout");std::this_thread::yield();}
}
Packet row(unsigned i){Packet p;p.sequence=i;p.source_us=std::int64_t(i)*100000;p.width=2;p.values[0]=i;p.values[1]=-double(i);return p;}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{
        fs::path root=argv[1];if(fs::exists(root))throw std::runtime_error("new result directory required");fs::create_directories(root);
        std::ofstream reports(root/"outcomes.tsv");
        for(const std::string mode:{"normal","abandoned","limited","late"}){
            auto identity=std::make_shared<FileIdentity>();
            auto sink=std::make_unique<NativeSink>(root/(mode+".jsonl"),identity);
            auto block=std::make_shared<Gate>();
            if(mode=="abandoned")sink->write_gate=block;
            if(mode=="late")sink->stop_gate=block;
            if(mode=="limited")sink->limits.seconds=0.15;
            Controller c(2,2,2*sizeof(Packet),std::move(sink));auto producer=c.producer();
            producer.push(row(1));
            if(mode=="abandoned")await(block->entered);
            producer.push(row(2));
            if(mode=="abandoned")producer.push(row(3));
            producer.finish(mode=="abandoned"?3:2);
            if(mode=="late"){
                await(block->entered);const auto timeout=c.drain(5ms);
                if(timeout.outcome!=Outcome::TimedOut||timeout.may_seal())throw std::runtime_error("late footer timeout missed");
            }
            block->resume=true;
            // For late: repeated drain only waits for cleanup; it cannot erase the timeout.
            auto r=c.drain(5s);
            if(!r.worker_done)throw std::runtime_error("worker has not released its writer");
            reports<<mode<<'\t'<<int(r.outcome)<<'\t'<<r.may_seal()<<'\t'<<r.accepted<<'\t'<<r.written<<'\t'<<r.sink_reason
                <<'\t'<<r.producer_done<<'\t'<<r.worker_done<<'\t'<<r.capture_counts_final<<'\t'<<r.drain_timed_out
                <<'\t'<<r.attempted<<'\t'<<r.queue_full<<'\t'<<r.invalid<<'\t'<<r.after_failure<<'\t'<<r.after_end
                <<'\t'<<r.wrong_producer<<'\t'<<r.declared_last<<'\t'<<r.last_written<<'\t'<<r.first_rejected<<'\t'<<r.discarded
                <<'\t'<<identity->nonce<<'\t'<<identity->bytes<<'\t'<<identity->sha<<'\t'<<r.after_end_wrong_producer<<'\n';
            const auto expected=mode=="normal"?Outcome::Complete:mode=="abandoned"?Outcome::QueueFull:mode=="limited"?Outcome::WriterLimit:Outcome::TimedOut;
            if(r.outcome!=expected)throw std::runtime_error("wrong native outcome: "+mode);
        }
        std::cout<<"Native worker: normal, abandon, writer limit, late complete footer exercised\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
