// Input shim core (INPUT-SHIM.md r3.1 sections 6, 7, 9 and 12): device epochs and rearm, the resolver, the test run.
#include "shim.hpp"
#include <algorithm>
#include <cstring>
namespace dbce::shim {
namespace {
bool pedal_waits_for_rest(int action){const std::string a=action_name(action);return a=="throttle"||a=="handbrake";}
long pov_raw(std::uint32_t v){return v==0xFFFFFFFFu||(v&0xFFFFu)==0xFFFFu?-1L:long(v);}
const char* call_name(Call c){
    switch(c){case Call::Ok:return "ok";case Call::AcquisitionLost:return "acquisition lost";case Call::Gone:return "device gone";case Call::NotCalled:return "not called";case Call::Pending:return "data not yet available";}
    return "?";
}
}

Core::Core(Provider& p,CoreConfig cfg):p_(p),cfg_(std::move(cfg)){
    bound_of_.fill(-1);
    start_us_=p_.now_us();
    run_=cfg_.latched?Run::Latched:Run::None;
    published_run_=run_;
    frame_.latched=cfg_.latched;
    frame_.suppress_native=cfg_.latched;
    frame_.run=run_;
}

void Core::set_profile(const Profile& profile){
    profile_=profile;
    const auto& s=profile_.section;
    for(int i=0;i<ActionCount;++i){
        const char* why=cfg_.capability.unsupported[std::size_t(i)];
        base_[std::size_t(i)]=why?Status::Unsupported:Status::Unbound;
        base_reason_[std::size_t(i)]=why?why:"";
    }
    usable_=profile_.ok&&s.error.empty();
    if(usable_){
        for(const auto& [key,why]:s.invalid){
            invalid_keys_.push_back(key);
            const int i=action_index(key);
            if(i>=0&&!cfg_.capability.unsupported[std::size_t(i)]){base_[std::size_t(i)]=Status::Invalid;base_reason_[std::size_t(i)]=why;}
        }
        for(const auto& e:s.bound){
            const int i=action_index(e.action);
            if(i<0)continue;
            in_profile_[std::size_t(i)]=true;
            if(cfg_.capability.unsupported[std::size_t(i)])continue;
            // One device per instance. Bindings that disagree on the instance's product are invalid, never merged.
            int d=-1;
            for(std::size_t k=0;k<devs_.size();++k)if(devs_[k].id.dev==e.binding.dev)d=int(k);
            if(d>=0&&devs_[std::size_t(d)].id.prod!=e.binding.prod){
                base_[std::size_t(i)]=Status::Invalid;
                base_reason_[std::size_t(i)]="prod "+e.binding.prod+" differs from the other bindings of "+e.binding.dev;
                invalid_keys_.push_back(e.action);
                continue;
            }
            if(d<0){
                Device dev;dev.id={e.binding.dev,e.binding.prod,e.binding.name};
                devs_.push_back(dev);d=int(devs_.size())-1;
            }
            bound_of_[std::size_t(i)]=int(bound_.size());
            bound_.push_back({i,e.binding,d});
            base_[std::size_t(i)]=Status::Opening;
        }
    }
    worker_epochs_.assign(devs_.size(),0);
    worker_caps_.assign(devs_.size(),ObjectCaps{});
    worker_interface_paths_.assign(devs_.size(),std::string{});
    last_dev_status_.assign(devs_.size(),std::string());
    last_status_.fill(Status::Unbound);
    if(cfg_.latched&&(!usable_||bound_.empty())){
        const std::string why=!profile_.ok?profile_.error:!s.error.empty()?s.error:"no applied action";
        set_run(Run::Inert,"the profile is not usable: "+why,start_us_);
    }
}

// ---------------------------------------------------------------- provider worker
bool Core::service_devices(){
    std::vector<Returned> back;
    {std::lock_guard<std::mutex> l(m_);back.swap(returned_);}
    bool did=!back.empty();
    for(const auto& r:back){
        // The input thread handed this handle back, so nothing reads it any more: reacquire (a new epoch) or close.
        bool again=false;
        if(r.why==Call::AcquisitionLost)
            for(int t=0;t<cfg_.reacquire_tries&&!again;++t)again=p_.reacquire(r.handle)==Call::Ok;
        if(!again){p_.close(r.handle);worker_interface_paths_[std::size_t(r.device)].clear();}
        std::lock_guard<std::mutex> l(m_);
        auto& d=devs_[std::size_t(r.device)];
        if(again){
            const auto epoch=++worker_epochs_[std::size_t(r.device)];
            opened_.push_back({r.device,r.handle,worker_caps_[std::size_t(r.device)],epoch,worker_interface_paths_[std::size_t(r.device)]});
            d.worker_state=DevState::Open;d.worker_reason.clear();
        }else{
            d.worker_state=DevState::Missing;d.worker_reason=std::string("read failed (")+call_name(r.why)+"), reopening";
            missing_scan_after_us_=0;   // a fresh loss looks for the device now, not after another device's wait
        }
    }
    std::vector<int> need;
    {std::lock_guard<std::mutex> l(m_);
     for(std::size_t i=0;i<devs_.size();++i){
         const auto st=devs_[i].worker_state;
         if(st==DevState::Missing||st==DevState::ProductMismatch)need.push_back(int(i));
     }}
    if(need.empty()){missing_scan_after_us_=0;return did;}
    // Only absent devices wait: connected reads and loss handling above are never throttled. All waiting devices
    // share one enumeration. A deadline more than one interval ahead means the clock went back: scan now.
    const auto now=p_.now_us();
    if(cfg_.missing_rescan_us>0&&missing_scan_after_us_&&now<missing_scan_after_us_&&
       missing_scan_after_us_-now<=cfg_.missing_rescan_us)return did;
    const auto listed=p_.enumerate();   // outside the lock: it may be slow
    did=true;
    int opens=0;bool opened_any=false;
    for(const int i:need){
        auto& d=devs_[std::size_t(i)];
        const DeviceInfo* found=nullptr;
        for(const auto& x:listed)if(x.dev==d.id.dev)found=&x;
        auto set=[&](DevState st,const std::string& why){std::lock_guard<std::mutex> l(m_);d.worker_state=st;d.worker_reason=why;};
        if(!found){set(DevState::Missing,"not attached");continue;}
        if(found->prod!=d.id.prod){set(DevState::ProductMismatch,"instance "+d.id.dev+" is product "+found->prod+", the binding names "+d.id.prod);continue;}
        if(opens>=cfg_.max_opens_per_pass)break;
        ++opens;
        set(DevState::Opening,"opening");
        auto o=p_.open(*found);
        if(!o.ok){set(DevState::Missing,"open failed: "+o.why);continue;}
        if(o.opened.dev!=found->dev||o.opened.prod!=found->prod){
            p_.close(o.handle);
            set(DevState::ProductMismatch,"the identity changed during the open (now "+o.opened.dev+" "+o.opened.prod+")");
            continue;
        }
        for(int a=0;a<8;++a)if(o.caps.max[std::size_t(a)]<=o.caps.min[std::size_t(a)])o.caps.range_ok[std::size_t(a)]=false;
        const auto epoch=++worker_epochs_[std::size_t(i)];
        worker_caps_[std::size_t(i)]=o.caps;
        worker_interface_paths_[std::size_t(i)]=o.opened.interface_path;
        std::lock_guard<std::mutex> l(m_);
        opened_.push_back({i,o.handle,o.caps,epoch,o.opened.interface_path});
        d.worker_state=DevState::Open;d.worker_reason.clear();
        opened_any=true;
    }
    // Keep filling a batch immediately when some devices opened. Once only absent or refused devices remain, wait
    // missing_rescan_us before the next EnumDevices instead of repeating it on every 5 ms worker pass.
    missing_scan_after_us_=opened_any||cfg_.missing_rescan_us<=0?0:now+cfg_.missing_rescan_us;
    return did;
}

// ---------------------------------------------------------------- control worker
void Core::request_admission(const Session& s,std::uint64_t now_unix,bool fences_verified,bool force_absent){
    // The session's end is latched on the monotonic clock now, so a slow admission never extends it (slice 1 review 2).
    const auto now=p_.now_us();
    std::lock_guard<std::mutex> l(m_);
    if(control_closed_)return;
    if(admission_requested_||admission_refused_){++admission_repeats_;return;}   // never refreshed, never a new nonce
    if(s.expires_unix<=now_unix){admission_refused_=true;admission_why_="the session has already ended";return;}
    const auto remaining=s.expires_unix-now_unix;
    if(remaining>3600){admission_refused_=true;admission_why_="the session ends more than an hour from now";return;}
    admission_requested_=true;session_=s;session_now_unix_=now_unix;fences_verified_=fences_verified;force_absent_=force_absent;
    admission_deadline_us_=now+std::int64_t(remaining)*1000000;
}
// m_ held. Bounded: an overflow is counted, and the input thread fails a running test on it (slice 1 review 4).
void Core::push_control_event(Event e){
    if(pending_events_.size()>=cfg_.control_event_queue){++control_events_dropped_;return;}
    pending_events_.push_back(std::move(e));
}
void Core::refuse_admission(const std::string& why){
    std::lock_guard<std::mutex> l(m_);
    if(control_closed_)return;
    admission_refused_=true;admission_why_=why;
}
Core::ControlState Core::control_state()const{
    std::lock_guard<std::mutex> l(m_);return {published_run_,control_closed_};
}
void Core::fail_control(const std::string& why){
    std::lock_guard<std::mutex> l(m_);
    if(!control_closed_&&!control_failed_){control_failed_=true;control_failure_=bounded_diagnostic(why);}
}
void Core::close_control(){std::lock_guard<std::mutex> l(m_);control_closed_=true;}
BatchResult Core::offer_batch(const std::string& bytes){
    BatchContext ctx;
    Run run;
    {std::lock_guard<std::mutex> l(m_);
     if(control_closed_){BatchResult closed;closed.verdict=BatchVerdict::Refused;closed.why="control admission is closed";return closed;}
     run=published_run_;ctx.nonce=session_.nonce;ctx.owned=workload_snapshot_;}
    ctx.have_last=have_last_;ctx.last_n=last_n_;ctx.last_hash=last_hash_;ctx.profile=&profile_.section;
    BatchResult r;
    const auto now=p_.now_us();
    if(run!=Run::Waiting&&run!=Run::Running){
        r.verdict=BatchVerdict::Refused;
        r.why=std::string("no test run is accepting batches (run ")+run_name(run)+")";
        // A terminal run stays terminal: logged once, then only counted; never a reason to rearm.
        std::lock_guard<std::mutex> l(m_);
        if(refusals_after_failure_++==0)push_control_event({0,now,EventKind::BatchRefused,-1,0,0,r.why});
        return r;
    }
    r=check_batch(bytes,ctx);
    r.generation.accepted_us=now;
    std::lock_guard<std::mutex> l(m_);
    // The host may have stopped, or the input thread ended the run, while parsing.
    if(control_closed_||(published_run_!=Run::Waiting&&published_run_!=Run::Running)){
        r.verdict=BatchVerdict::Refused;r.why="control admission closed while checking batch";return r;
    }
    if(r.verdict==BatchVerdict::Accepted&&generations_.size()>=cfg_.generation_queue){
        r.verdict=BatchVerdict::Refused;r.why="the activation queue is full";
    }
    switch(r.verdict){
    case BatchVerdict::Accepted:
        // Bounded by the generation queue; each one is accounted for again at activation or as superseded.
        generations_.push_back(r.generation);
        have_last_=true;last_n_=r.generation.n;last_hash_=r.generation.hash;
        push_control_event({0,now,EventKind::BatchAccepted,-1,0,r.generation.n,
            hex(r.generation.hash)+" commands "+std::to_string(r.generation.commands.size())+(r.generation.end?" end":"")+
            (r.generation.used_raw?" raw":"")+(r.generation.used_action?" action":"")});
        break;
    case BatchVerdict::Duplicate:
        // Coalesced into one event per tick with a count, so a flood costs a counter, not memory.
        ++duplicates_pending_;duplicate_last_n_=r.generation.n;
        break;
    case BatchVerdict::Refused:
        if(!batch_failed_){batch_failed_=true;batch_failed_why_=r.why;push_control_event({0,now,EventKind::BatchRefused,-1,0,r.generation.n,r.why});}
        else ++refusals_after_failure_;
        break;
    }
    return r;
}
std::vector<Event> Core::drain_events(){
    std::lock_guard<std::mutex> l(m_);
    std::vector<Event> out(event_q_.begin(),event_q_.end());event_q_.clear();
    return out;
}
std::vector<StatusChange> Core::drain_status(){
    std::lock_guard<std::mutex> l(m_);
    std::vector<StatusChange> out(status_q_.begin(),status_q_.end());status_q_.clear();
    return out;
}
JournalCut Core::drain_journal(){
    std::lock_guard<std::mutex> l(m_);
    JournalCut c;
    c.events.assign(event_q_.begin(),event_q_.end());event_q_.clear();
    c.status.assign(status_q_.begin(),status_q_.end());status_q_.clear();
    c.status_dropped=status_dropped_;c.status_coalesced=status_coalesced_;c.events_dropped=events_dropped_;c.control_events_dropped=control_events_dropped_;
    return c;
}
std::uint64_t Core::status_dropped()const{std::lock_guard<std::mutex> l(m_);return status_dropped_;}
std::uint64_t Core::status_coalesced()const{std::lock_guard<std::mutex> l(m_);return status_coalesced_;}
std::uint64_t Core::events_dropped()const{std::lock_guard<std::mutex> l(m_);return events_dropped_;}
std::uint64_t Core::control_events_dropped()const{std::lock_guard<std::mutex> l(m_);return control_events_dropped_;}

// ---------------------------------------------------------------- input thread
const Frame& Core::tick(){
    const auto start=p_.now_us();
    frame_.seq++;
    frame_.tick_us=start;
    frame_.samples.clear();
    frame_.down=frame_.up=0;
    adopt_worker_changes(start);
    read_devices(start);
    // The decision time is re-read after every device read (slice 1 review 1): a slow read must not let an older sample
    // or an expired command through. Admission, expiry, activation, delivery and freshness all use it.
    const auto now=p_.now_us();
    frame_.boundary_us=now;
    for(auto& s:frame_.samples)if(s.valid&&now-s.read_end_us>cfg_.fresh_us)s.stale=true;
    {bool closed,failed;std::string why;
     {std::lock_guard<std::mutex> l(m_);closed=control_closed_;failed=control_failed_;why=control_failure_;}
     if((closed||failed)&&(run_==Run::Latched||run_==Run::Waiting||run_==Run::Running))
         set_run(Run::Failed,failed?"control I/O failed: "+why:"control admission closed",now);}
    if(run_==Run::Latched)evaluate_admission(now);
    {bool failed=false,overflow=false;std::string why;
     {std::lock_guard<std::mutex> l(m_);failed=batch_failed_;why=batch_failed_why_;overflow=control_events_dropped_>0;}
     if(failed&&(run_==Run::Waiting||run_==Run::Running))set_run(Run::Failed,"batch refused: "+why,now);
     if(overflow&&(run_==Run::Waiting||run_==Run::Running))set_run(Run::Failed,"control events overflowed (the test evidence would be incomplete)",now);}
    if(run_==Run::Waiting||run_==Run::Running){
        std::string why;
        if(!workload_intact(why))set_run(Run::Failed,why,now);
        else if(now>=expires_us_)set_run(Run::Expired,"the session expired",now);
        else if(run_==Run::Waiting&&now-admitted_us_>=cfg_.wait_first_us)set_run(Run::Failed,"no command within the first-batch wait",now);
    }
    activate_generations(now);
    deliver(now);
    resolve(now);
    std::uint64_t digital=0;
    for(int i=0;i<ActionCount;++i)if(is_digital(i)&&frame_.action[std::size_t(i)].value>=0.5f)digital|=1ull<<i;
    frame_.down=digital&~prev_digital_;frame_.up=prev_digital_&~digital;prev_digital_=digital;
    track_status(now);
    frame_.run=run_;
    frame_.event_seq=event_seq_;
    return frame_;
}

void Core::adopt_worker_changes(std::int64_t now){
    std::vector<Opened> op;
    std::vector<Event> control_events;
    std::uint64_t duplicates=0,duplicate_n=0;
    {std::lock_guard<std::mutex> l(m_);
     op.swap(opened_);
     control_events.swap(pending_events_);
     duplicates=duplicates_pending_;duplicate_n=duplicate_last_n_;duplicates_pending_=0;
     for(auto& d:devs_)if(d.state!=DevState::Open){
         // An epoch the input thread handed back stays read-failed until the worker reopens or gives up on it.
         if(d.state==DevState::ReadFailed&&d.worker_state==DevState::ReadFailed)continue;
         d.state=d.worker_state==DevState::Open?DevState::Opening:d.worker_state;
         d.reason=d.worker_reason;
     }}
    for(auto& e:control_events)emit_event(e);
    if(duplicates)emit(EventKind::BatchDuplicate,now,-1,0,duplicate_n,std::to_string(duplicates)+" exact duplicate(s), ignored");
    for(const auto& o:op){
        auto& d=devs_[std::size_t(o.device)];
        d.state=DevState::Open;d.handle=o.handle;d.caps=o.caps;d.epoch=o.epoch;d.fresh_seen=false;d.pending=false;d.reason.clear();
        d.interface_path=o.interface_path;
        emit(EventKind::EpochBegin,now,o.device,o.epoch,0,d.id.dev);
        // Rearm (section 6): physical values in an ordinary process only.
        for(const auto& b:bound_)if(b.device==o.device){
            auto& r=rearm_[std::size_t(b.action)];
            r.need_release=is_digital(b.action);
            r.need_rest=pedal_waits_for_rest(b.action);
        }
    }
}

void Core::read_devices(std::int64_t now){
    for(std::size_t i=0;i<devs_.size();++i){
        auto& d=devs_[i];
        d.sample=-1;
        if(d.state!=DevState::Open)continue;
        DeviceSample s;
        s.device=std::uint32_t(i);s.epoch=d.epoch;s.seq=++d.seq;
        s.read_start_us=p_.now_us();
        RawState st;                                  // initialized: axes 0, buttons up, POVs centred
        s.poll=p_.poll(d.handle);
        if(s.poll==Call::Ok)s.state=p_.state(d.handle,st);   // Poll fills nothing; only a complete state read counts
        s.read_end_us=p_.now_us();
        s.valid=s.poll==Call::Ok&&s.state==Call::Ok;
        const bool pending=s.poll==Call::Pending||s.state==Call::Pending;
        if(s.valid){
            s.physical=st;s.delivered=st;
            d.fresh_seen=true;d.last_valid_us=s.read_end_us;d.pending=false;
        }else if(pending&&(!d.pending||now-d.pending_since_us<cfg_.pending_limit_us)){
            // Data not yet available (E_PENDING): no sample, the epoch stays open and is read again next tick. The
            // device's actions are neutral this frame and rearm, so a held button never returns as a new press.
            if(!d.pending){
                d.pending=true;d.pending_since_us=now;
                emit(EventKind::ReadPending,now,int(i),d.epoch,0,d.id.dev);
                for(const auto& b:bound_)if(b.device==int(i)){
                    auto& r=rearm_[std::size_t(b.action)];
                    r.need_release=is_digital(b.action);
                    r.need_rest=pedal_waits_for_rest(b.action);
                }
            }
        }else{
            // A lost or gone device, or pending for longer than pending_limit_us (then: reacquire the same handle).
            const Call why=pending?Call::AcquisitionLost:s.poll!=Call::Ok?s.poll:s.state;
            d.pending=false;
            emit(EventKind::EpochEnd,now,int(i),d.epoch,0,std::string("read failed: ")+call_name(why));
            {std::lock_guard<std::mutex> l(m_);
             returned_.push_back({int(i),d.handle,why,d.epoch});
             d.worker_state=DevState::ReadFailed;d.worker_reason=std::string("read failed (")+call_name(why)+")";}
            d.state=DevState::ReadFailed;d.reason=std::string("read failed (")+call_name(why)+")";
            d.handle=0;d.fresh_seen=false;d.interface_path.clear();
        }
        d.sample=int(frame_.samples.size());
        frame_.samples.push_back(s);
    }
}

bool Core::owned_ready()const{
    if(devs_.empty())return false;
    for(const auto& d:devs_){
        if(d.state!=DevState::Open||!d.fresh_seen||d.sample<0)return false;
        const auto& s=frame_.samples[std::size_t(d.sample)];
        if(!s.valid||s.stale)return false;
    }
    return true;
}
// Supported bindings whose object exists, with a readable range for axes, on a device with a fresh valid read in this
// frame (slice 1 review 3: device readiness alone admitted a profile whose only binding was missing).
int Core::usable_bindings()const{
    int n=0;
    for(const auto& b:bound_){
        const auto& d=devs_[std::size_t(b.device)];
        if(d.state!=DevState::Open||d.sample<0)continue;
        const auto& s=frame_.samples[std::size_t(d.sample)];
        if(!s.valid||s.stale)continue;
        const auto i=std::size_t(b.binding.index);
        switch(b.binding.kind){
        case ctl::Kind::Axis:if(d.caps.axis[i]&&d.caps.range_ok[i]&&d.caps.max[i]>d.caps.min[i])++n;break;
        case ctl::Kind::Button:if(d.caps.button.test(i))++n;break;
        case ctl::Kind::Hat:if(d.caps.pov[i])++n;break;
        default:break;
        }
    }
    return n;
}

void Core::evaluate_admission(std::int64_t now){
    bool requested,refused,fences,force;std::int64_t deadline;std::string why;
    {std::lock_guard<std::mutex> l(m_);
     requested=admission_requested_;refused=admission_refused_;fences=fences_verified_;force=force_absent_;
     deadline=admission_deadline_us_;why=admission_why_;}
    if(refused){set_run(Run::Failed,"admission refused: "+why,now);return;}
    const bool late=now-start_us_>=cfg_.admission_us;
    if(!requested){if(late)set_run(Run::Failed,"no admission request",now);return;}
    // The deadline was latched when the request arrived; waiting for devices never extends it (slice 1 review 2).
    if(now>=deadline){set_run(Run::Failed,"admission refused: the session ended before admission",now);return;}
    if(!fences){set_run(Run::Failed,"admission refused: a fence is not verified",now);return;}
    if(!force){set_run(Run::Failed,"admission refused: a force path exists",now);return;}
    if(!owned_ready()){
        if(late){
            std::string which;
            for(const auto& d:devs_)if(d.state!=DevState::Open||!d.fresh_seen)which+=(which.empty()?"":", ")+d.id.dev+" "+device_status(d);
            set_run(Run::Failed,"admission refused: devices not ready ("+which+")",now);
        }
        return;
    }
    if(!usable_bindings()){set_run(Run::Failed,"admission refused: no usable binding (every applied action's object is missing or unqualified)",now);return;}
    workload_.clear();
    std::vector<OwnedDevice> snapshot;
    for(std::size_t i=0;i<devs_.size();++i){workload_.push_back({int(i),devs_[i].epoch});snapshot.push_back({devs_[i].id.dev,devs_[i].caps});}
    admitted_us_=now;
    expires_us_=deadline;
    {std::lock_guard<std::mutex> l(m_);workload_snapshot_=snapshot;}
    set_run(Run::Waiting,"admitted ("+std::to_string(workload_.size())+" device(s), "+std::to_string(usable_bindings())+" usable binding(s))",now);
}

bool Core::workload_intact(std::string& why)const{
    for(const auto& [i,epoch]:workload_){
        const auto& d=devs_[std::size_t(i)];
        if(d.state!=DevState::Open||d.epoch!=epoch){
            why="epoch "+std::to_string(epoch)+" of "+d.id.dev+" ended ("+device_status(d)+(d.reason.empty()?"":": "+d.reason)+")";
            return false;
        }
        if(d.sample<0||!frame_.samples[std::size_t(d.sample)].valid){why="no valid read of "+d.id.dev+" in this frame";return false;}
        if(frame_.samples[std::size_t(d.sample)].stale){
            why="the sample of "+d.id.dev+" is "+std::to_string((frame_.boundary_us-frame_.samples[std::size_t(d.sample)].read_end_us)/1000)+
                " ms old at the frame's decision time (bound "+std::to_string(cfg_.fresh_us/1000)+" ms)";
            return false;
        }
    }
    return true;
}

void Core::activate_generations(std::int64_t now){
    if(run_terminal(run_)){
        // Every accepted batch gets an event: anything still queued is superseded, never silently dropped.
        std::deque<Generation> left;
        {std::lock_guard<std::mutex> l(m_);left.swap(generations_);}
        for(const auto& g:left)emit(EventKind::BatchSuperseded,now,-1,0,g.n,std::string("run ")+run_name(run_)+" before activation");
        return;
    }
    if(run_!=Run::Waiting&&run_!=Run::Running)return;
    Generation g;bool have=false;
    {std::lock_guard<std::mutex> l(m_);
     if(!generations_.empty()){g=std::move(generations_.front());generations_.pop_front();have=true;}}
    if(!have)return;
    emit(EventKind::BatchActivated,now,-1,0,g.n,hex(g.hash.data(),8));
    running_.clear();
    if(g.end){set_run(Run::Completed,"end command in batch "+std::to_string(g.n),now);return;}
    for(const auto& c:g.commands){
        Running r;r.cmd=c;r.deadline_us=now+std::int64_t(c.ms)*1000;r.batch=g.n;r.hash=g.hash;
        for(std::size_t i=0;i<devs_.size();++i)if(devs_[i].id.dev==c.dev)r.device=int(i);
        running_.push_back(r);
    }
    if(run_==Run::Waiting)set_run(Run::Running,"batch "+std::to_string(g.n)+" activated",now);
}

void Core::deliver(std::int64_t now){
    for(auto it=running_.begin();it!=running_.end();){
        if(now>=it->deadline_us){
            const auto& d=devs_[std::size_t(it->device)];
            emit(EventKind::ObjectExpired,now,it->device,d.epoch,it->batch,
                 std::string(ctl::kindName(it->cmd.kind))+" "+std::to_string(it->cmd.index)+" after "+std::to_string(it->cmd.ms)+" ms");
            it=running_.erase(it);
        }else ++it;
    }
    if(run_!=Run::Running)return;
    for(const auto& r:running_){
        auto& d=devs_[std::size_t(r.device)];
        if(d.sample<0)continue;
        auto& s=frame_.samples[std::size_t(d.sample)];
        if(!s.valid)continue;   // unreachable while the workload is intact (frame boundary)
        const auto i=std::size_t(r.cmd.index);
        switch(r.cmd.kind){
        case ctl::Kind::Axis:
            s.delivered.axes[i]=std::int32_t(ctl::rescale(r.cmd.raw,r.cmd.bind_min,r.cmd.bind_max,d.caps.min[i],d.caps.max[i]));
            s.injected.axes|=std::uint8_t(1u<<i);break;
        case ctl::Kind::Button:
            s.delivered.buttons[i]=r.cmd.raw?0x80:0;s.injected.buttons.set(i);break;
        case ctl::Kind::Hat:
            s.delivered.povs[i]=r.cmd.raw<0?0xFFFFFFFFu:std::uint32_t(r.cmd.raw);s.injected.povs|=std::uint8_t(1u<<i);break;
        default:break;
        }
        s.injected.batch=r.batch;
        std::memcpy(s.injected.hash,r.hash.data(),8);
    }
}

void Core::resolve(std::int64_t now){
    const bool test=cfg_.latched;
    for(int i=0;i<ActionCount;++i){
        auto& a=frame_.action[std::size_t(i)];
        a=ActionValue{};
        const int bi=bound_of_[std::size_t(i)];
        if(bi<0){a.status=base_[std::size_t(i)];continue;}
        const auto& b=bound_[std::size_t(bi)];
        const auto& d=devs_[std::size_t(b.device)];
        a.sample=std::int16_t(d.sample);
        switch(d.state){
        case DevState::Missing:a.status=Status::DeviceMissing;continue;
        case DevState::ProductMismatch:a.status=Status::ProductMismatch;continue;
        case DevState::Opening:a.status=Status::Opening;continue;
        case DevState::ReadFailed:a.status=Status::ReadFailed;continue;
        case DevState::Open:break;
        }
        if(!d.fresh_seen||d.sample<0||!frame_.samples[std::size_t(d.sample)].valid){a.status=Status::ReadPending;continue;}
        if(frame_.samples[std::size_t(d.sample)].stale||now-d.last_valid_us>cfg_.fresh_us){a.status=Status::Stale;continue;}
        const auto& k=b.binding;const auto idx=std::size_t(k.index);
        if(k.kind==ctl::Kind::Axis){
            if(!d.caps.axis[idx]){a.status=Status::ObjectMissing;continue;}
            if(!d.caps.range_ok[idx]||d.caps.max[idx]<=d.caps.min[idx]){a.status=Status::RangeUnqualified;continue;}
        }else if(k.kind==ctl::Kind::Button){if(!d.caps.button.test(idx)){a.status=Status::ObjectMissing;continue;}}
        else if(k.kind==ctl::Kind::Hat){if(!d.caps.pov[idx]){a.status=Status::ObjectMissing;continue;}}
        const auto& s=frame_.samples[std::size_t(d.sample)];
        const bool marked=k.kind==ctl::Kind::Axis?(s.injected.axes>>idx)&1u:k.kind==ctl::Kind::Button?s.injected.buttons.test(idx):(s.injected.povs>>idx)&1u;
        auto raw_of=[&](const RawState& st)->long{
            switch(k.kind){
            case ctl::Kind::Axis:return ctl::rescale(st.axes[idx],d.caps.min[idx],d.caps.max[idx],k.min,k.max);
            case ctl::Kind::Button:return ctl::diButtonDown(st.buttons[idx])?1L:0L;
            case ctl::Kind::Hat:return pov_raw(st.povs[idx]);
            default:return 0L;
            }};
        const char* name=action_name(i);
        if(test){
            // A latched process: only a delivered injected value reaches an action; physical values are observed in the
            // sample but feed nothing, not even rearm (section 6, "which source the rules read").
            if(run_==Run::Running&&marked){a.status=Status::Live;a.injected=true;a.value=float(ctl::normalize(name,k,raw_of(s.delivered)));}
            else a.status=Status::TestNeutral;
            continue;
        }
        const double n=ctl::normalize(name,k,raw_of(s.physical));
        auto& r=rearm_[std::size_t(i)];
        if(r.need_release){
            if(n>=0.5){a.status=Status::Rearming;continue;}
            r.need_release=false;
        }
        if(r.need_rest){
            if(n>cfg_.rest_threshold){a.status=Status::Rearming;continue;}
            r.need_rest=false;
        }
        a.status=Status::Live;a.value=float(n);
    }
}

void Core::set_run(Run r,const std::string& why,std::int64_t now){
    if(run_==r||run_terminal(run_))return;   // terminal states are final
    run_=r;run_reason_=why;
    if(run_terminal(r))running_.clear();
    emit(EventKind::RunChanged,now,-1,0,0,std::string(run_name(r))+": "+why);
    {std::lock_guard<std::mutex> l(m_);published_run_=r;}
    queue_status({now,StatusChange::Of::Run,0,run_name(r),why});
}

void Core::emit(EventKind k,std::int64_t us,int device,std::uint32_t epoch,std::uint64_t batch,const std::string& detail){
    emit_event({0,us,k,device,epoch,batch,detail});
}
void Core::emit_event(Event e){
    e.seq=++event_seq_;
    ring_[std::size_t(e.seq%EventRingSize)]=e;
    std::lock_guard<std::mutex> l(m_);
    if(event_q_.size()>=cfg_.event_queue){++events_dropped_;return;}
    event_q_.push_back(std::move(e));
}

void Core::queue_status(StatusChange c){
    std::lock_guard<std::mutex> l(m_);
    for(auto& q:status_q_)if(q.of==c.of&&q.index==c.index){q=std::move(c);++status_coalesced_;return;}
    if(status_q_.size()>=cfg_.status_queue){++status_dropped_;return;}
    status_q_.push_back(std::move(c));
}

std::string Core::device_status(const Device& d)const{
    switch(d.state){
    case DevState::Missing:return "device-missing";case DevState::ProductMismatch:return "product-mismatch";
    case DevState::Opening:return "opening";case DevState::ReadFailed:return "read-failed";
    case DevState::Open:return d.fresh_seen?"live":"read-pending";
    }
    return "?";
}
std::string Core::device_reason(const Device& d)const{
    std::string r=d.id.dev+(d.id.name.empty()?"":" \""+d.id.name+"\"");
    if(!d.reason.empty())r+=": "+d.reason;
    return r;
}

void Core::track_status(std::int64_t now){
    for(int i=0;i<ActionCount;++i){
        const auto st=frame_.action[std::size_t(i)].status;
        std::string why;
        const int bi=bound_of_[std::size_t(i)];
        const int device=bi<0?-1:bound_[std::size_t(bi)].device;
        const auto epoch=device<0?0u:devs_[std::size_t(device)].epoch;
        if(bi<0)why=base_reason_[std::size_t(i)];
        else if(st==Status::ObjectMissing||st==Status::RangeUnqualified){
            const auto& k=bound_[std::size_t(bi)].binding;
            why=std::string(st==Status::ObjectMissing?"no ":"unreadable or empty range on ")+ctl::kindName(k.kind)+" "+std::to_string(k.index)+
                " of "+device_reason(devs_[std::size_t(bound_[std::size_t(bi)].device)]);
        }
        else if(st==Status::Stale)why="the sample is older than "+std::to_string(cfg_.fresh_us/1000)+" ms at the frame's decision time: "+
                                     device_reason(devs_[std::size_t(bound_[std::size_t(bi)].device)]);
        else if(is_unavailable(st))why=device_reason(devs_[std::size_t(bound_[std::size_t(bi)].device)]);
        else if(st==Status::Rearming)why=pedal_waits_for_rest(i)?"waiting for rest":"waiting for release";
        if(st!=last_status_[std::size_t(i)]||why!=last_reason_[std::size_t(i)]||epoch!=last_status_epoch_[std::size_t(i)]){
            last_status_[std::size_t(i)]=st;last_reason_[std::size_t(i)]=why;
            last_status_epoch_[std::size_t(i)]=epoch;
            queue_status({now,StatusChange::Of::Action,i,status_name(st),why,device,epoch});
        }
    }
    for(std::size_t i=0;i<devs_.size();++i){
        const auto st=device_status(devs_[i])+" epoch "+std::to_string(devs_[i].epoch);
        if(st!=last_dev_status_[i]){last_dev_status_[i]=st;queue_status({now,StatusChange::Of::Device,int(i),st,device_reason(devs_[i]),int(i),devs_[i].epoch});}
    }
}

StartReport Core::start_report()const{
    StartReport r;
    if(!profile_.ok){r.line="controls: nothing applied via shim-ini-controls-1: "+profile_.error;return r;}
    if(!profile_.section.error.empty()){r.line="controls: nothing applied via shim-ini-controls-1: "+profile_.section.error;return r;}
    std::vector<std::string> applied,unsupported,unavailable;
    for(int i=0;i<ActionCount;++i){
        if(!in_profile_[std::size_t(i)])continue;
        const auto& a=frame_.action[std::size_t(i)];
        const auto why=last_reason_[std::size_t(i)];
        if(cfg_.capability.unsupported[std::size_t(i)]){
            unsupported.push_back(action_name(i));
            r.notes.push_back(std::string("controls: ")+action_name(i)+" unsupported: "+cfg_.capability.unsupported[std::size_t(i)]);
        }else if(a.status==Status::Invalid){
            // counted with the invalid lines below
        }else if(is_applied(a.status)){
            applied.push_back(action_name(i));
            if(a.status!=Status::Live&&a.status!=Status::TestNeutral)r.notes.push_back(std::string("controls: ")+action_name(i)+" "+status_name(a.status)+": "+why);
        }else{
            unavailable.push_back(action_name(i));
            r.notes.push_back(std::string("controls: ")+action_name(i)+" "+status_name(a.status)+": "+why);
        }
    }
    std::vector<std::string> invalid;
    for(const auto& [key,why]:profile_.section.invalid){invalid.push_back(key);r.notes.push_back("controls: "+key+" invalid: "+why);}
    for(int i=0;i<ActionCount;++i)if(base_[std::size_t(i)]==Status::Invalid&&std::find(invalid.begin(),invalid.end(),action_name(i))==invalid.end()){
        invalid.push_back(action_name(i));r.notes.push_back(std::string("controls: ")+action_name(i)+" invalid: "+base_reason_[std::size_t(i)]);}
    auto list=[](const std::vector<std::string>& v){std::string s;for(const auto& x:v)s+=(s.empty()?"":",")+x;return s;};
    const auto& s=profile_.section;
    r.line="controls: profile \""+s.profile+"\" revision "+(s.revision.empty()?std::string("-"):s.revision)+" via shim-ini-controls-1: applied "+
           std::to_string(applied.size())+" ("+list(applied)+"); unsupported "+std::to_string(unsupported.size())+" ("+list(unsupported)+
           "); invalid "+std::to_string(invalid.size())+" ("+list(invalid)+"); unavailable "+std::to_string(unavailable.size())+" ("+list(unavailable)+")";
    return r;
}
std::vector<DeviceReport> Core::device_reports()const{
    // All fields read here belong to the input thread or are immutable after set_profile. Never read worker_state,
    // worker_reason, pending opens or native objects. A failed/closed epoch cannot advertise stale capabilities.
    std::vector<DeviceReport> out;out.reserve(devs_.size());
    for(const auto& d:devs_){
        const bool opened=d.state==DevState::Open;
        auto identity=d.id;
        identity.interface_path=opened?d.interface_path:std::string{};
        out.push_back({std::move(identity),d.epoch,opened,opened?d.caps:ObjectCaps{},device_status(d)});
    }
    return out;
}
}
