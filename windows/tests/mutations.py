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

FENCE = [
    ("run folder not exclusive", "source/test_fence.cpp",
     "if(!CreateDirectoryW(plan.run_root.c_str(),nullptr)){", "if(!CreateDirectoryW(plan.run_root.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS){"),
    ("links in the owner's state followed", "source/test_fence.cpp",
     "if(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){plan.refusal=", "if(false){plan.refusal="),
    ("a linked test folder accepted", "source/test_fence.cpp",
     "if(has_reparse_point_on_path(plan.dbce_folder)){", "if(false){"),
    ("no file bound", "source/test_fence.cpp",
     "if(plan.seeded.size()+1>limits.max_files){", "if(false){"),
    ("no byte bound", "source/test_fence.cpp",
     "if(plan.seeded_bytes+size>limits.max_bytes){", "if(false){"),
    ("the CRT keeps OR2_ switches", "source/test_fence.cpp",
     "_wputenv_s(n.c_str(),L\"\");", ""),
    ("home stays the game folder when latched", "source/test_fence.cpp",
     "return plan.latched&&plan.refusal.empty()?plan.run_root:normalize(game_root);", "return normalize(game_root);"),
    ("the game folder's former cache migrated when read only", "../src/system/setup_screen.cpp",
     "if(!game_dir_read_only)adopt_former_cache(root,cache,fallback);", "adopt_former_cache(root,cache,fallback);"),
    ("options rename that cannot replace (the Windows bug)", "../src/enhancements/video.cpp",
     "std::error_code ec;std::filesystem::rename(temp,path,ec);", "std::error_code ec;if(std::rename(temp.c_str(),path.c_str())!=0)ec=std::make_error_code(std::errc::file_exists);"),
    ("seed copy not verified (no fixture produces a differing copy)", "source/test_fence.cpp",
     "if(!ra||!rb||ha!=hb||a!=b){", "if(false){"),
]

DINPUT = [
    ("generation not checked at admission", "shim/dinput_provider.cpp",
     "if(generation_of(w)!=generation||(low_of(w)&(Closed|Reserved))){++misuse;return nullptr;}",
     "if(low_of(w)&(Closed|Reserved)){++misuse;return nullptr;}"),
    ("admission not one compare-exchange (check, then add)", "shim/dinput_provider.cpp",
     "if(s.word.compare_exchange_weak(w,w+1,std::memory_order_acq_rel))break;", "s.word.fetch_add(1);break;"),
    ("no stop check at admission (the check before and the back-out after the compare-exchange)", "shim/dinput_provider.cpp",
     "            if(stopped.load()){++refused;return nullptr;}\n            if(s.word.compare_exchange_weak(w,w+1,std::memory_order_acq_rel))break;\n        }\n        if(stopped.load()){leave(s);++refused;return nullptr;}   // the stop raced this admission: back out",
     "            if(s.word.compare_exchange_weak(w,w+1,std::memory_order_acq_rel))break;\n        }"),
    ("opens and listings admitted after stop", "shim/dinput_provider.cpp",
     "if(stopped.load()){worker_ops.fetch_sub(1);++refused;return false;}", ""),
    ("DirectInput released under an open in flight", "shim/dinput_provider.cpp",
     "if(!rep.held&&st->worker_ops.load()==0&&st->input){", "if(!rep.held&&st->input){"),
    ("a read holds the provider's storage, not the shared state", "shim/dinput_provider.cpp",
     "    const auto st=state_;   // keeps everything this read touches alive, even if the provider is destroyed meanwhile",
     "    auto& st=state_;"),
    ("shutdown releases a device being read", "shim/dinput_provider.cpp",
     "if((low_of(w)&(Closed|Reserved|Readers))!=0)return false;", "if((low_of(w)&(Closed|Reserved))!=0)return false;"),
    ("E_PENDING treated as a gone device", "shim/dinput_provider.cpp",
     "    if(hr==E_PENDING)return Call::Pending;   // \"data not yet available\": transient, the device is not lost\n", ""),
    ("no rearm after a pending frame", "shim/core.cpp",
     "                emit(EventKind::ReadPending,now,int(i),d.epoch,0,d.id.dev);\n                for(const auto& b:bound_)if(b.device==int(i)){",
     "                emit(EventKind::ReadPending,now,int(i),d.epoch,0,d.id.dev);\n                for(const auto& b:bound_)if(false){"),
    ("pending retried forever", "shim/core.cpp",
     "}else if(pending&&(!d.pending_since_us||now-d.pending_since_us<cfg_.pending_limit_us)){", "}else if(pending){"),
    ("state copied out of a failed read", "shim/dinput_provider.cpp",
     "if(c!=Call::Ok)return c;   // nothing is copied from a failed or pending read", ""),
    ("requested range assumed, not read back", "shim/dinput_provider.cpp",
     "if(SUCCEEDED(d->GetProperty(DIPROP_RANGE,&back.diph))&&back.lMax>back.lMin){",
     "back.lMin=0;back.lMax=65535;if(true){"),
    ("objects assumed, not checked by offset", "shim/dinput_provider.cpp",
     "r.caps.axis[i]=has(AxisOffsets[a]);", "r.caps.axis[i]=true;"),
    ("identity taken from the listing, not the handle", "shim/dinput_provider.cpp",
     "r.opened={guid_text(di.guidInstance),guid_text(di.guidProduct),narrow(di.tszProductName)};", "r.opened=listed;"),
    ("acquisition loss classified as gone", "shim/dinput_provider.cpp",
     "if(hr==DIERR_INPUTLOST||hr==DIERR_NOTACQUIRED)return Call::AcquisitionLost;", ""),
    ("a refused open handle not released", "shim/dinput_provider.cpp",
     "auto fail=[&](const std::string& why){d->Release();r=OpenResult{};r.why=why;return r;};",
     "auto fail=[&](const std::string& why){r=OpenResult{};r.why=why;return r;};"),
]

SETS = {"core": ("test_shim", CORE), "adapter": ("test_outrun_adapter", ADAPTER), "fence": ("test_fence", FENCE),
        "dinput": ("test_dinput_provider", DINPUT)}

def run(target, mutations):
    build = ["cmake", "--build", str(repo / "build-xbox-pc"), "--config", "Release", "--target", target]
    exe = repo / "build-xbox-pc" / "windows" / "Release" / (target + ".exe")
    results = []
    for name, rel, old, new in mutations:
        path = repo / "windows" / rel
        original = path.read_bytes()
        text = original.decode("utf-8")
        eol = "\r\n" if "\r\n" in text else "\n"           # patterns are written with \n; match the file's line ends
        old, new = old.replace("\n", eol), new.replace("\n", eol)
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
