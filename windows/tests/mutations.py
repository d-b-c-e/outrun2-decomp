# Run: python -I windows/tests/mutations.py <repo root> [core|adapter]
# Mutation check for the input shim (windows/docs/INPUT-SHIM.md): each mutation breaks one rule; the named test program
# must fail for every one. Rebuilds once per mutation and restores every file byte for byte.
import pathlib, subprocess, sys

repo = pathlib.Path(sys.argv[1])
only = sys.argv[2] if len(sys.argv) > 2 else None

CORE = [
    ("frame boundary skipped", "shim/core.cpp",
     "if(!workload_intact(why))set_run(Run::Failed,why,now);", "if(false)set_run(Run::Failed,why,now);"),
    ("physical value reaches an action in a latched process", "shim/core.cpp",
     "else a.status=Status::TestNeutral;", "else{a.status=Status::Live;a.value=float(ctl::normalize(name,k,raw_of(s.physical)));}"),
    ("single latest generation (FIFO collapsed)", "shim/core.cpp",
     "if(!generations_.empty()){g=std::move(generations_.front());generations_.pop_front();have=true;}",
     "if(!generations_.empty()){g=std::move(generations_.back());generations_.clear();have=true;}"),
    ("sample admitted after Poll alone", "shim/core.cpp",
     "s.valid=s.poll==Call::Ok&&s.state==Call::Ok;", "s.valid=s.poll==Call::Ok;"),
    ("duplicate batch re-accepted (TTL extended)", "shim/parse.cpp",
     "if(g.hash==ctx.last_hash){r.verdict=BatchVerdict::Duplicate;", "if(false){r.verdict=BatchVerdict::Duplicate;"),
    ("no digital rearm after an epoch start", "shim/core.cpp",
     "r.need_release=is_digital(b.action);", "r.need_release=false;"),
    ("throttle rearm dropped", "shim/core.cpp",
     "r.need_rest=pedal_waits_for_rest(b.action);", "r.need_rest=false;"),
    ("terminal state not final (unreachable guard)", "shim/core.cpp",
     "if(run_==r||run_terminal(run_))return;", "if(run_==r)return;"),
    ("refused batch does not fail the run", "shim/core.cpp",
     "if(!batch_failed_){batch_failed_=true;", "if(!batch_failed_){batch_failed_=false;"),
    ("product mismatch tolerated", "shim/core.cpp",
     "if(found->prod!=d.id.prod){", "if(false){"),
    ("identity not re-checked after open", "shim/core.cpp",
     "if(o.opened.dev!=found->dev||o.opened.prod!=found->prod){", "if(false){"),
    ("axis range readback ignored", "shim/core.cpp",
     "if(!d.caps.range_ok[idx]||d.caps.max[idx]<=d.caps.min[idx]){a.status=Status::RangeUnqualified;continue;}", ""),
    ("object existence ignored", "shim/core.cpp",
     "if(!d.caps.axis[idx]){a.status=Status::ObjectMissing;continue;}", ""),
    ("superseded batches dropped silently", "shim/core.cpp",
     "for(const auto& g:left)emit(EventKind::BatchSuperseded", "for(const auto& g:left)if(false)emit(EventKind::BatchSuperseded"),
    ("unknown session key accepted", "shim/parse.cpp",
     "}else{why=\"inject.on has an unknown key", "}else if(false){why=\"inject.on has an unknown key"),
    ("duplicate profile key accepted", "shim/parse.cpp",
     "for(const auto& k:keys)if(k.first==key){", "for(const auto& k:keys)if(false){"),
    ("injection delivered without the run running (unreachable guard)", "shim/core.cpp",
     "if(run_!=Run::Running)return;\n    for(const auto& r:running_){", "for(const auto& r:running_){"),
    ("decision time = tick start", "shim/core.cpp",
     "const auto now=p_.now_us();\n    frame_.boundary_us=now;", "const auto now=start;\n    frame_.boundary_us=now;"),
    ("stale samples not marked", "shim/core.cpp",
     "for(auto& s:frame_.samples)if(s.valid&&now-s.read_end_us>cfg_.fresh_us)s.stale=true;", ""),
    ("admission deadline not enforced", "shim/core.cpp",
     "if(now>=deadline){set_run(Run::Failed,\"admission refused: the session ended before admission\",now);return;}", ""),
    ("repeated admission request refreshes", "shim/core.cpp",
     "if(admission_requested_||admission_refused_){++admission_repeats_;return;}", "if(admission_refused_){++admission_repeats_;return;}"),
    ("admission without a usable binding", "shim/core.cpp",
     "if(!usable_bindings()){set_run(", "if(false){set_run("),
    ("control inbox unbounded", "shim/core.cpp",
     "if(pending_events_.size()>=cfg_.control_event_queue){++control_events_dropped_;return;}", ""),
    ("duplicates not coalesced", "shim/core.cpp",
     "++duplicates_pending_;duplicate_last_n_=r.generation.n;", "push_control_event({0,now,EventKind::BatchDuplicate,-1,0,r.generation.n,\"\"});"),
    ("control overflow not a failure", "shim/core.cpp",
     "if(overflow&&(run_==Run::Waiting||run_==Run::Running))", "if(false)"),
]

ADAPTER = [
    ("native input not suppressed in a latched process", "source/outrun_adapter.cpp",
     "if(f.suppress_native)signals_.native_suppressed=suppress_native(out);", ""),
    ("stop path lost under suppression", "source/outrun_adapter.cpp",
     "o.exit_requested=exit;o.connected=connected;", "o.exit_requested=false;o.connected=connected;"),
    ("latched process left on the PC layer (redundant: suppression already clears it)", "source/outrun_adapter.cpp",
     "if(steer||throttle||brake||f.suppress_native)out.pc_input_layer=false;", "if(steer||throttle||brake)out.pc_input_layer=false;"),
    ("pedal overrides the pad instead of max-merging", "source/outrun_adapter.cpp",
     "native=std::max(native,v);", "native=v;"),
    ("pedal record overrides the pad", "source/outrun_adapter.cpp",
     "d.axes_94[word]=std::int16_t(std::max<std::uint32_t>(std::uint16_t(d.axes_94[word]),v));", "d.axes_94[word]=std::int16_t(v);"),
    ("pedals gated on steering", "source/outrun_adapter.cpp",
     "pedal(throttle,\"throttle\",out.accelerator,0x0d);", "pedal(throttle&&steer,\"throttle\",out.accelerator,0x0d);"),
    ("confirm sets the race shift bit", "source/outrun_adapter.cpp",
     "    if(!race){\n        if(held(f,\"confirm\"))d.buttons_04|=0x2u;", "    {\n        if(held(f,\"confirm\"))d.buttons_04|=0x2u;"),
    ("gearbox ignores the deadline", "source/outrun_adapter.cpp",
     "if(now<deadline_us_)return 0;", ""),
    ("gearbox retries forever", "source/outrun_adapter.cpp",
     "if(++tries_>cfg_.retries){", "if(false){"),
    ("ambiguous gates take the last gate", "source/outrun_adapter.cpp",
     "if(count>1){target_=0;set(State::Ambiguous,\"ambiguous: \"+std::to_string(count)+\" gates held\");return 0;}", ""),
    ("no released tick after a pulse", "source/outrun_adapter.cpp",
     "        set(State::Release,\"released after a shift pulse\");   // at least one released tick\n        return 0;",
     "        set(State::Release,\"released after a shift pulse\");   // at least one released tick\n        [[fallthrough]];"),
    ("scene change keeps the gearbox", "source/outrun_adapter.cpp",
     "if(scene_change){gearbox_.reset();gearbox_.released_tick();pulse_prev_=0;last_mode_=g.mode;}", "if(scene_change){last_mode_=g.mode;}"),
    ("invalid gear word accepted (slice 2a review 1)", "source/outrun_adapter.cpp",
     "if(gear<1||gear>6){set(State::Unavailable,", "if(false){set(State::Unavailable,"),
    ("no released tick across a gate change (slice 2a review 2)", "source/outrun_adapter.cpp",
     "    if(just_pulsed)return 0;\n", ""),
    ("pulse on the scene change's own tick", "source/outrun_adapter.cpp",
     "const int dir=scene_change?0:gearbox_.step(", "const int dir=gearbox_.step("),
    ("pulse without its shift edge", "source/outrun_adapter.cpp",
     "out.shift_up|=down(f,\"shiftUp\")||(pulse_down&0x2u);", "out.shift_up|=down(f,\"shiftUp\");"),
    ("owned WGI pad not dropped", "source/outrun_adapter.cpp",
     "if(!mine)return int(i);", "return int(i);"),
    ("latched stop frame keeps the pad", "source/outrun_adapter.cpp",
     "if(latched)signals_.native_suppressed=suppress_native(out);", ""),
]

SETS = {"core": ("test_shim", CORE), "adapter": ("test_outrun_adapter", ADAPTER)}

def run(target, mutations):
    build = ["cmake", "--build", str(repo / "build-xbox-pc"), "--config", "Release", "--target", target]
    exe = repo / "build-xbox-pc" / "windows" / "Release" / (target + ".exe")
    results = []
    for name, rel, old, new in mutations:
        path = repo / "windows" / rel
        original = path.read_bytes()
        text = original.decode("utf-8")
        if text.count(old) != 1:
            results.append((name, f"PATTERN NOT FOUND ({text.count(old)})"))
            continue
        try:
            path.write_bytes(text.replace(old, new).encode("utf-8"))
            if subprocess.run(build, capture_output=True, text=True).returncode != 0:
                results.append((name, "BUILD FAILED"))
                continue
            try:
                t = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
                results.append((name, ("caught: " + (t.stderr.strip() or t.stdout.strip())) if t.returncode else "NOT CAUGHT"))
            except subprocess.TimeoutExpired:
                results.append((name, "caught: timeout"))
        finally:
            path.write_bytes(original)
    subprocess.run(build, capture_output=True, text=True)
    return results

total = caught = 0
for key, (target, mutations) in SETS.items():
    if only and key != only:
        continue
    print(f"{target}:")
    for name, verdict in run(target, mutations):
        total += 1
        caught += verdict.startswith("caught")
        print(f"- {name}: {verdict}")
print(f"{caught} of {total} caught")
