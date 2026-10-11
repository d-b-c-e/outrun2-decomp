#include "observer_mapping.hpp"
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>
using namespace dbce::shim::observe;
static int checks=0;
static void need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
static Payload pattern(std::uint32_t generation){Payload p{};for(std::size_t i=0;i<p.size();++i)p[i]=generation^std::uint32_t(i*2654435761u);return p;}
static bool valid(const Snapshot& s){const auto p=pattern(s.words[0]);return s.sequence==std::uint64_t(s.words[0])*2 && s.words==p;}

int wmain(int argc,wchar_t** argv){
    try{
        if(argc==4 && std::wstring(argv[1])==L"--reader"){
            Mapping m(argv[2],false);Snapshot s;need(read(m.storage(),s)&&valid(s),"child reads coherent initial snapshot");
            HANDLE e=OpenEventW(EVENT_MODIFY_STATE,FALSE,argv[3]);need(e!=nullptr,"child event opened");
            // Park midway through atomic reads; the parent kills this exact child.
            std::uint32_t partial=0;for(std::size_t i=0;i<PayloadWords/2;++i)partial^=m.storage().words[i].load(std::memory_order_seq_cst);
            (void)partial;need(SetEvent(e)!=0,"child mid-read signalled");CloseHandle(e);
            HANDLE never=CreateEventW(nullptr,TRUE,FALSE,nullptr);WaitForSingleObject(never,INFINITE);return 2;
        }
        need(argc==1,"unexpected arguments");
        Storage local;Snapshot s;s.sequence=123;
        need(!read(local,s)&&s.sequence==123,"unpublished read preserves previous output");
        Writer w(local);need(w.publish(pattern(1)),"first publication");need(read(local,s)&&valid(s),"coherent first snapshot");
        bool other=false;std::thread wrong([&]{other=w.publish(pattern(2));});wrong.join();need(!other,"wrong input thread refused");
        Writer competitor(local);need(!competitor.publish(pattern(1)),"second writer cannot replace established sequence");
        local.sequence.store(3);need(!read(local,s,32)&&s.sequence==2,"in-progress bounded read preserves prior output");local.sequence.store(2);
        // Deterministic overlap: advance the same production writer halfway
        // through the copy. The exact production reader logic must reject it.
        bool published=false;
        const bool torn=detail::read_words(local,s,1,[&](std::size_t i){
            if(i==PayloadWords/2)published=w.publish(pattern(2));
            return local.words[i].load(std::memory_order_seq_cst);
        });
        need(published&&!torn&&s.sequence==2,"changed generation during read is refused without replacing prior snapshot");
        need(read(local,s)&&valid(s)&&s.words[0]==2,"read recovers after concurrent publication");

        Storage concurrent;std::atomic<bool> ready{false},done{false};std::atomic<unsigned> successes{0},failures{0};
        std::thread producer([&]{Writer writer(concurrent);ready=true;for(std::uint32_t g=1;g<=2000;++g)if(!writer.publish(pattern(g)))++failures;done=true;});
        std::vector<std::thread> readers;
        for(int k=0;k<3;++k)readers.emplace_back([&]{while(!ready.load())std::this_thread::yield();std::uint64_t previous=0;do{Snapshot snap;if(read(concurrent,snap)){if(!valid(snap)||snap.sequence<previous)++failures;previous=snap.sequence;++successes;}}while(!done.load());});
        producer.join();for(auto& r:readers)r.join();need(successes>0&&failures==0,"three overlapping readers never see a torn or reversed generation");
        need(read(concurrent,s)&&s.words[0]==2000&&valid(s),"final concurrent snapshot");

        const auto suffix=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        const auto name=L"Local\\DbceShimObserver-"+suffix,eventName=name+L"-entered";
        Mapping owner(name,true);Writer mapped(owner.writable());need(mapped.publish(pattern(1)),"mapped publish");
        {Mapping reader(name,false);need(read(reader.storage(),s)&&valid(s),"FILE_MAP_READ snapshot");bool refused=false;try{(void)reader.writable();}catch(const std::logic_error&){refused=true;}need(refused,"reader cannot request writable storage");}
        bool duplicate=false;try{Mapping second(name,true);}catch(const std::runtime_error&){duplicate=true;}need(duplicate,"existing mapping ownership refused");
        HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());need(entered!=nullptr,"child barrier created");
        wchar_t self[32768];const auto n=GetModuleFileNameW(nullptr,self,32768);need(n>0&&n<32768,"own image path");
        auto command=L"\""+std::wstring(self)+L"\" --reader \""+name+L"\" \""+eventName+L"\"";
        STARTUPINFOW si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};
        need(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=0,"owned reader child created");
        struct Child{PROCESS_INFORMATION& p;~Child(){if(WaitForSingleObject(p.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(p.hProcess,99);WaitForSingleObject(p.hProcess,5000);}CloseHandle(p.hThread);CloseHandle(p.hProcess);}} child{pi};
        need(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0,"child parked halfway through snapshot");CloseHandle(entered);
        const auto begin=std::chrono::steady_clock::now();for(std::uint32_t g=2;g<=101;++g)need(mapped.publish(pattern(g)),"writer progresses while reader parked");
        need(std::chrono::steady_clock::now()-begin<std::chrono::seconds(2),"reader cannot block writer");
        need(TerminateProcess(pi.hProcess,73)!=0&&WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0,"owned reader ended within deadline");
        DWORD code=0;need(GetExitCodeProcess(pi.hProcess,&code)&&code==73,"owned reader exit observed");
        need(mapped.publish(pattern(102))&&read(owner.storage(),s)&&valid(s),"writer and subsequent read survive reader death");
        std::printf("PASS %d observer transport checks; %u coherent overlapping reads; no game/device/force\n",checks,successes.load());return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
