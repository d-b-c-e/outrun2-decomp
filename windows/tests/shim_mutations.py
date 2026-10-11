# Run: python -I windows/tests/shim_mutations.py <repo root>  (rebuilds test_shim once per mutation; restores every file byte for byte)
"""Mutation check for the input shim slice 1: each mutation breaks one rule in core.cpp or parse.cpp;
the test binary must fail for every one. Restores the original file after each run."""
import pathlib, subprocess, sys

repo = pathlib.Path(sys.argv[1])
build = ["cmake", "--build", str(repo / "build-xbox-pc"), "--config", "Release", "--target", "test_shim"]
exe = repo / "build-xbox-pc" / "windows" / "Release" / "test_shim.exe"

M = [
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
    ("terminal state not final", "shim/core.cpp",
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
    ("injection delivered without the run running", "shim/core.cpp",
     "if(run_!=Run::Running)return;\n    for(const auto& r:running_){", "for(const auto& r:running_){"),
    ("decision time = tick start (review 1)", "shim/core.cpp",
     "const auto now=p_.now_us();\n    frame_.boundary_us=now;", "const auto now=start;\n    frame_.boundary_us=now;"),
    ("stale samples not marked (review 1)", "shim/core.cpp",
     "for(auto& s:frame_.samples)if(s.valid&&now-s.read_end_us>cfg_.fresh_us)s.stale=true;", ""),
    ("admission deadline not enforced (review 2)", "shim/core.cpp",
     "if(now>=deadline){set_run(Run::Failed,\"admission refused: the session ended before admission\",now);return;}", ""),
    ("repeated admission request refreshes (review 2)", "shim/core.cpp",
     "if(admission_requested_||admission_refused_){++admission_repeats_;return;}", "if(admission_refused_){++admission_repeats_;return;}"),
    ("admission without a usable binding (review 3)", "shim/core.cpp",
     "if(!usable_bindings()){set_run(", "if(false){set_run("),
    ("control inbox unbounded (review 4)", "shim/core.cpp",
     "if(pending_events_.size()>=cfg_.control_event_queue){++control_events_dropped_;return;}", ""),
    ("duplicates not coalesced (review 4)", "shim/core.cpp",
     "++duplicates_pending_;duplicate_last_n_=r.generation.n;", "push_control_event({0,now,EventKind::BatchDuplicate,-1,0,r.generation.n,\"\"});"),
    ("control overflow not a failure (review 4)", "shim/core.cpp",
     "if(overflow&&(run_==Run::Waiting||run_==Run::Running))", "if(false)"),
]

results = []
for name, rel, old, new in M:
    path = repo / "windows" / rel
    original = path.read_bytes().decode("utf-8")
    if original.count(old) != 1:
        results.append((name, f"PATTERN NOT FOUND ({original.count(old)})"))
        continue
    try:
        path.write_bytes(original.replace(old, new).encode("utf-8"))
        b = subprocess.run(build, capture_output=True, text=True)
        if b.returncode != 0:
            results.append((name, "BUILD FAILED"))
            continue
        try:
            t = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
            verdict = "caught: " + (t.stderr.strip() or t.stdout.strip()) if t.returncode != 0 else "NOT CAUGHT"
        except subprocess.TimeoutExpired:
            verdict = "caught: timeout"
        results.append((name, verdict))
    finally:
        path.write_bytes(original.encode("utf-8"))

subprocess.run(build, capture_output=True, text=True)
for name, verdict in results:
    print(f"- {name}: {verdict}")
print("ALL CAUGHT" if all(v.startswith("caught") for _, v in results) else "SOME NOT CAUGHT")
