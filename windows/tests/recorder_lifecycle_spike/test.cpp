#include "worker.hpp"
#include <functional>
#include <iostream>
#include <limits>
#include <string>
using namespace recorder_spike;
using namespace std::chrono_literals;
namespace {
int checks=0;
void check(bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);}
void wait(const std::function<bool()>& ready){
    const auto until=std::chrono::steady_clock::now()+5s;
    while(!ready()){if(std::chrono::steady_clock::now()>=until)throw std::runtime_error("fixture barrier timeout");std::this_thread::yield();}
}
struct Gate {
    std::atomic<bool> enabled{false},entered{false},released{false};
    void arrive(){if(!enabled.exchange(false))return;entered=true;while(!released)std::this_thread::yield();}
};
struct Trace {
    Gate write_gate,stop_gate;
    std::vector<Packet> rows; // only inspect after worker_done
    std::atomic<int> opens{0},writes{0},stops{0},abandons{0},destroys{0},owner_calls{0};
    std::atomic<bool> inner_complete{false};
    const std::thread::id owner=std::this_thread::get_id();
    std::string fail_at,throw_at;
    void call(){if(std::this_thread::get_id()==owner)++owner_calls;}
    void release(){write_gate.released=true;stop_gate.released=true;}
};
std::vector<std::weak_ptr<Trace>> traces;
std::shared_ptr<Trace> trace(){auto t=std::make_shared<Trace>();traces.push_back(t);return t;}
struct FakeSink final:Sink {
    std::shared_ptr<Trace> t;
    explicit FakeSink(std::shared_ptr<Trace> s):t(std::move(s)){}
    ~FakeSink()override{t->call();++t->destroys;}
    bool step(const char* name){t->call();if(t->throw_at==name)throw std::runtime_error(name);return t->fail_at!=name;}
    bool open()override{++t->opens;return step("open");}
    bool write(const Packet& p)override{t->write_gate.arrive();if(!step("write"))return false;t->rows.push_back(p);++t->writes;return true;}
    bool stop()override{++t->stops;t->stop_gate.arrive();if(!step("stop"))return false;t->inner_complete=true;return true;}
    void abandon()noexcept override{t->call();++t->abandons;}
    std::string reason()const override{return t->fail_at.empty()?(t->inner_complete?"stopped":"abandoned"):t->fail_at+"Failure";}
};
struct EmptyGate final:Hooks {Gate gate;void after_empty_observation()override{gate.arrive();}};
Packet packet(std::uint64_t seq){Packet p;p.sequence=seq;p.source_us=std::int64_t(seq)*100;p.width=2;p.values[0]=double(seq);p.values[1]=-double(seq);return p;}
std::unique_ptr<Controller> controller(std::shared_ptr<Trace> t,std::size_t cap=4,std::shared_ptr<Hooks> hooks={}){
    return std::make_unique<Controller>(cap,2,cap*sizeof(Packet),std::make_unique<FakeSink>(std::move(t)),1,std::move(hooks));
}
void reconcile(const Report& r){
    check(r.attempted==r.accepted+r.queue_full+r.invalid+r.after_failure+r.after_end+r.wrong_producer,"attempt reconciliation");
    check(r.written<=r.accepted,"written cannot exceed accepted");
    if(r.capture_counts_final)check(r.accepted==r.written+r.discarded,"accepted packet reconciliation");
    if(r.may_seal()){
        check(r.producer_done&&r.accepted==r.written&&r.accepted!=0,"completion has ordered producer end and exact nonempty drain");
        check(r.last_written==r.declared_last,"completion has exact declared tail");
    }
}
void done(Producer& p){wait([&]{return p.report().worker_done;});}

void normal(){
    auto t=trace();t->write_gate.enabled=true;auto c=controller(t);auto p=c->producer();
    auto first=packet(1);check(p.push(first),"first accepted");wait([&]{return t->write_gate.entered.load();});
    first.values[0]=999;check(p.push(packet(2))&&p.push(packet(3)),"producer proceeds while sink is paused");
    p.finish(3);t->release();const auto r=c->drain(2s);done(p);
    check(r.may_seal()&&t->inner_complete&&t->stops==1&&t->abandons==0,"clean drain completes");
    check(t->rows.size()==3&&t->rows[0].values[0]==1&&t->rows[2].source_us==300&&t->rows[2].sequence==3,"owned snapshots and source times preserved");
    check(t->owner_calls==0&&t->destroys==1,"all sink calls and destruction on worker");
    check(r.ring_bytes==4*sizeof(Packet),"actual fixed ring payload accounted");
    check(!p.push(packet(4)),"after finish refused");c->cancel();const auto after=p.report();
    check(after.outcome==Outcome::Complete&&after.after_end==1,"completion first is not downgraded by later cancellation");reconcile(after);
}

void overflow(){
    auto t=trace();t->write_gate.enabled=true;auto c=controller(t,2);auto p=c->producer();p.push(packet(1));
    wait([&]{return t->write_gate.entered.load();});
    check(p.push(packet(2)),"remaining slot usable during write");
    const auto start=std::chrono::steady_clock::now();const bool full=p.push(packet(3));const auto elapsed=std::chrono::steady_clock::now()-start;
    check(!full&&elapsed<1s,"full queue refuses without waiting on blocked sink");
    check(!p.push(packet(4)),"sticky loss refuses future rows");p.finish(4);t->release();const auto r=c->drain(2s);done(p);
    check(r.outcome==Outcome::QueueFull&&!r.may_seal()&&r.queue_full==1&&r.after_failure==1,"overflow is sticky and counted");
    check(r.accepted==2&&r.written==1&&t->stops==0&&t->abandons==1,"ring remainder abandoned, no clean footer");reconcile(r);
    check(r.first_rejected&&r.first_rejected_sequence==3&&r.first_rejected_us==300&&r.first_rejected_reason==Outcome::QueueFull,"first lost packet recorded");
}

void validation(){
    for(int mode=0;mode<5;++mode){
        auto t=trace();auto c=controller(t);auto p=c->producer();p.push(packet(1));auto bad=packet(2);
        if(mode==0)bad.sequence=3;
        if(mode==1)bad.source_us=100;
        if(mode==2)bad.source_us=-1;
        if(mode==3)bad.values[1]=std::numeric_limits<double>::quiet_NaN();
        if(mode==4)bad.width=1025;
        check(!p.push(bad),"invalid packet refused");p.finish(2);const auto r=c->drain(2s);done(p);
        check(r.outcome==Outcome::Invalid&&r.invalid==1&&!r.may_seal()&&t->stops==0,"invalid data cannot complete");reconcile(r);
    }
    {auto t=trace();auto c=controller(t);auto p=c->producer();p.finish(0);const auto r=c->drain(2s);done(p);check(r.outcome==Outcome::Empty&&!r.may_seal(),"empty capture refused");reconcile(r);}
    {auto t=trace();auto c=controller(t);auto p=c->producer();p.push(packet(1));p.finish(2);const auto r=c->drain(2s);done(p);check(r.outcome==Outcome::TrailingLoss&&!r.may_seal()&&t->stops==0,"trailing loss refused");reconcile(r);}
}

void wrong_thread(){
    auto t=trace();auto c=controller(t);auto p=c->producer();p.push(packet(1));bool accepted=true;
    std::thread other([&]{accepted=p.push(packet(2));});other.join();p.finish(1);const auto r=c->drain(2s);done(p);
    check(!accepted&&r.outcome==Outcome::WrongProducer&&r.wrong_producer==1&&!r.may_seal(),"foreign producer rejected with sticky failure");reconcile(r);
}

void sink_failures(){
    for(const std::string where:{"open","write","stop"})for(bool throwing:{false,true}){
        auto t=trace();(throwing?t->throw_at:t->fail_at)=where;auto c=controller(t);auto p=c->producer();p.push(packet(1));p.finish(1);
        const auto r=c->drain(2s);done(p);
        const auto expected=throwing?Outcome::SinkException:where=="open"?Outcome::OpenFailure:where=="write"?Outcome::WriteFailure:Outcome::StopFailure;
        check(r.outcome==expected&&!r.may_seal(),"sink failure contained");
        check(t->abandons==1&&t->destroys==1&&t->owner_calls==0,"failure cleanup worker-owned");reconcile(r);
    }
}

void late_stop(bool cancel){
    auto t=trace();t->stop_gate.enabled=true;auto c=controller(t);auto p=c->producer();p.push(packet(1));p.finish(1);
    wait([&]{return t->stop_gate.entered.load();});
    if(cancel)c->cancel();
    const auto timed=c->drain(5ms);check(!timed.may_seal()&&!timed.worker_done,"unresolved stop cannot seal");
    check(timed.outcome==(cancel?Outcome::Cancelled:Outcome::TimedOut),"first terminal outcome preserved");
    c.reset();t->release();done(p);const auto after=p.report();
    check(t->inner_complete&&after.outcome==timed.outcome&&!after.may_seal(),"late complete inner file cannot resurrect outer success");
    check(t->destroys==1&&t->owner_calls==0,"sink survives facade and is destroyed on worker");reconcile(after);
}

void missing_finish(){
    auto t=trace();auto c=controller(t);auto p=c->producer();p.push(packet(1));wait([&]{return t->writes.load()==1;});
    auto r=c->drain(5ms);check(r.outcome==Outcome::TimedOut&&!r.may_seal()&&t->stops==0,"supervisor cannot invent producer end");
    p.finish(1);done(p);check(p.report().outcome==Outcome::TimedOut,"late producer finish cannot undo timeout");reconcile(p.report());
}

void end_publication(){
    auto t=trace();auto h=std::make_shared<EmptyGate>();h->gate.enabled=true;auto c=controller(t,2,h);auto p=c->producer();
    wait([&]{return h->gate.entered.load();}); // worker observed empty before the last push and end
    p.push(packet(1));p.finish(1);h->gate.released=true;const auto r=c->drain(2s);done(p);
    check(r.may_seal()&&r.written==1&&t->rows.size()==1,"end acquire is followed by a fresh publication read");reconcile(r);
}

void producer_disappears(){
    auto t=trace();auto c=controller(t);{auto p=c->producer();p.push(packet(1));}
    const auto r=c->drain(2s);check(r.outcome==Outcome::ProducerAbandoned&&!r.may_seal()&&t->stops==0,"abandoned producer cannot complete");reconcile(r);
}
}
int main(){
    try{normal();overflow();validation();wrong_thread();sink_failures();late_stop(false);late_stop(true);missing_finish();end_publication();producer_disappears();}
    catch(const std::exception& e){for(auto& w:traces)if(auto t=w.lock())t->release();std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"Recorder lifecycle: "<<checks<<" checks passed\n";
}
