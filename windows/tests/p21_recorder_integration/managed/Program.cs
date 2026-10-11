// P21 out-of-process finalizer (P21-SHUTDOWN.md D8). A capture seals only with three independent facts bound to one
// capture: the worker's quiescent report and stream (the reviewed queued-frames validation, unchanged), the host's own
// "resolved" shutdown report, and the supervisor's observation of a normal exit by the same process identity.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using Dbce.Wheel.Playback;
using Dbce.Wheel.Recording;

static class Program {
    static readonly string[] Queued={"bundle.meta","profile.ini","worker.tsv","source.jsonl","budget.tsv"};
    static readonly string[] Required=Queued.Concat(new[]{"host-shutdown.json","exit.json"}).ToArray();
    const string Contract="dbce.shim.queued-frames-spike@2\n", InputContract="shim-frame-audit-2-pending-unadopted";
    // The OutRunDesktop latched capture (Astra 16553, R1): the same reviewed checks, with its own contract, its declared
    // schema size (the fixture's is 382), every recorded device, real-session file bounds and the devices.json member.
    const string DesktopContract="dbce.shim.outrun-capture@1\n";
    static bool Desktop(string dir)=>File.ReadAllText(Path.Combine(dir,"bundle.meta"))==DesktopContract;
    static string[] RequiredFor(string dir)=>Desktop(dir)?Required.Concat(new[]{"devices.json"}).ToArray():Required;
    static int checks;
    static void Check(bool ok,string why){++checks;if(!ok)throw new InvalidDataException(why);}
    static ulong U(string s){Check(ulong.TryParse(s,NumberStyles.None,CultureInfo.InvariantCulture,out var n),"unsigned decimal");return n;}
    static uint Limb(double n){Check(double.IsFinite(n)&&n>=0&&n<=uint.MaxValue&&n==Math.Floor(n),"uint32 limb");return (uint)n;}
    static ulong Wide(IDictionary<string,double> c,string k)=>Limb(c[k+".lo"])|((ulong)Limb(c[k+".hi"])<<32);
    static List<FileStream> Freeze(string dir,bool sealedBundle){
        var held=new List<FileStream>();var desktop=Desktop(dir);try{
            foreach(var name in RequiredFor(dir).Concat(sealedBundle?new[]{"complete.tsv"}:Array.Empty<string>())){
                var path=Path.GetFullPath(Path.Combine(dir,name));
                for(var p=path;p!=null;p=Path.GetDirectoryName(p))Check((File.GetAttributes(p)&FileAttributes.ReparsePoint)==0,"reparse path");
                var f=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read);held.Add(f);
                Check(f.Length<=(name=="source.jsonl"?(desktop?1024L*1024*1024:1024*1024):65536),"fixture file bound");
            }return held;
        }catch{foreach(var f in held)f.Dispose();throw;}
    }
    // Copied verbatim from windows/tests/queued_frames_spike/managed/Program.cs at 24e27bd (Validate): the reviewed
    // worker-report, nonce, closed-bytes, profile, footer, schema and row-bound checks. Keep in sync; do not weaken.
    static SessionSample[] ValidateQueued(string dir){
        var desktop=Desktop(dir);
        Check(desktop||File.ReadAllText(Path.Combine(dir,"bundle.meta"))==Contract,"known fixture contract");
        var lines=File.ReadAllLines(Path.Combine(dir,"worker.tsv"));Check(lines.Length==1,"one worker report");
        var p=lines[0].Split('\t');Check(p.Length==23,"known report layout");
        Check(p[0]=="2"&&p[11]=="1"&&p[12]=="1"&&p[22]=="stopped","worker completed and quiescent");
        for(int i=13;i<=21;++i)Check(p[i]=="0","no timeout or loss or diagnostic call in this fixture");
        var accepted=U(p[1]);var written=U(p[2]);var attempted=U(p[3]);var tail=U(p[4]);var last=U(p[5]);var first=U(p[6]);var origin=U(p[7]);
        Check(first>0&&tail>=first&&tail!=ulong.MaxValue&&accepted>0&&accepted==written&&attempted==accepted&&tail==last&&written==tail-first+1,"first/tail/count accounting");
        var records=SessionReader.Read(Path.Combine(dir,"source.jsonl")).ToArray();
        var m=records[0].Metadata;var footer=records.Last().Footer;
        Check(m.Game==(desktop?"outrun2006-decomp-desktop":"outrun-queued-frames-fixture")&&m.Properties["inputContract"]==InputContract,"known frame contract");
        Check(p[8].Length==32&&p[8].All(c=>"0123456789abcdef".Contains(c))&&m.Properties["captureNonce"]==p[8],"same capture nonce");
        Check((ulong)new FileInfo(Path.Combine(dir,"source.jsonl")).Length==U(p[9])&&ArtifactSeal.Hash(Path.Combine(dir,"source.jsonl"))==p[10],"worker-closed source bytes");
        Check(m.Properties["profileSha256"]==ArtifactSeal.Hash(Path.Combine(dir,"profile.ini")),"exact profile bytes");
        var count=footer.Counts;
        Check(footer.Completed&&footer.StopReason=="stopped"&&count.AcceptedSamples==(long)accepted&&count.WrittenSamples==(long)written&&
            count.DroppedSamples==0&&count.DroppedMarkers==0&&count.QueueFullCount==0&&count.ContentionCount==0&&count.InvalidCount==0&&
            count.LimitCount==0&&count.ErrorCount==0&&count.AcceptedMarkers==0&&count.WrittenMarkers==0,"inner worker accounting");
        Check(records.All(x=>x.Marker==null),"fixture has no event journal");
        var rows=records.Where(x=>x.Sample!=null).Select(x=>x.Sample).ToArray();Check((ulong)rows.Length==written,"complete rows");
        var declared=desktop?U(File.ReadAllText(Path.Combine(dir,"budget.tsv")).TrimEnd('\r','\n').Split('\t')[0]):382UL;
        var names=rows[0].Channels.Keys.OrderBy(x=>x,StringComparer.Ordinal).ToArray();Check((ulong)names.Length==declared,"complete two-device plus adapter schema and inputs");
        for(int i=0;i<rows.Length;++i){var row=rows[i];var c=row.Channels;var seq=Wide(c,"frame");var tick=Wide(c,"tickUs");
            Check(c.Keys.OrderBy(x=>x,StringComparer.Ordinal).SequenceEqual(names),"fixed complete schema");
            Check(seq==first+(ulong)i&&Wide(c,"adapter.frame")==seq&&tick>=origin&&row.ElapsedSeconds==(tick-origin)/1e6,"source sequence/time/adapter join");
            var devices=desktop?(int)c["sampleCount"]:2;
            for(int device=0;device<devices;++device)foreach(var call in new[]{"poll","state"}){
                var v=c[$"sample.{device}.{call}"];Check(v>=0&&v<=4&&v==Math.Floor(v),"known Call enum including Pending");
            }
        }
        var budget=File.ReadAllText(Path.Combine(dir,"budget.tsv")).TrimEnd('\r','\n').Split('\t');
        Check(budget.Length==3&&U(budget[0])==declared&&U(budget[2])<=4*1024*1024,"declared schema and queue bytes");
        var bound=192UL+names.Aggregate(0UL,(sum,n)=>sum+(ulong)System.Text.Json.JsonSerializer.Serialize(n).Length+34UL);
        Check(U(budget[1])==bound,"independent worst-case row bound");
        var data=File.ReadAllLines(Path.Combine(dir,"source.jsonl"));
        Check(Encoding.UTF8.GetByteCount(data[0])+1<=8192,"fixture metadata reserve");
        Check(data.Skip(1).Take(rows.Length).All(x=>(ulong)Encoding.UTF8.GetByteCount(x)+1<=bound),"every actual row fits declared bound");
        return rows;
    }
    // Typed reads: a missing or mistyped field refuses with its name, never a parser exception or a default.
    static JsonElement Prop(JsonElement o,string name,params JsonValueKind[] kinds){
        ++checks;if(o.ValueKind!=JsonValueKind.Object||!o.TryGetProperty(name,out var v))throw new InvalidDataException("missing "+name);
        Check(kinds.Contains(v.ValueKind),"wrong type: "+name);return v;}
    static string Str(JsonElement o,string n)=>Prop(o,n,JsonValueKind.String).GetString()!;
    static bool Bool(JsonElement o,string n)=>Prop(o,n,JsonValueKind.True,JsonValueKind.False).GetBoolean();
    static ulong Num(JsonElement o,string n){var v=Prop(o,n,JsonValueKind.Number);Check(v.TryGetUInt64(out var x),"not an unsigned integer: "+n);return x;}
    // The two P21 facts, bound to the worker report's nonce and to each other's nonzero process identity.
    static void ValidateHostAndExit(string dir){
        var nonce=File.ReadAllLines(Path.Combine(dir,"worker.tsv"))[0].Split('\t')[8];
        JsonDocument Parse(string name){try{return JsonDocument.Parse(File.ReadAllText(Path.Combine(dir,name)));}catch(JsonException x){throw new InvalidDataException("unreadable "+name,x);}}
        using var host=Parse("host-shutdown.json");using var exit=Parse("exit.json");
        var h=host.RootElement;var e=exit.RootElement;
        Check(Str(h,"state")=="resolved","host shutdown resolved");
        Check(Str(h,"nonce")==nonce,"host report names this capture");
        Check(Bool(h,"recorderJoined")&&Num(h,"recorderOutcome")==2,"recorder complete and its worker joined");
        Check(Prop(h,"unresolved",JsonValueKind.Array).GetArrayLength()==0,"nothing unresolved");
        // A failed identity query yields 0 on both sides; equal zeros identify nothing (Astra 12080).
        Check(Num(h,"pid")!=0&&Num(h,"creationTime")!=0&&Num(e,"pid")!=0&&Num(e,"creationTime")!=0,"nonzero process identity in both reports");
        Check(Num(e,"pid")==Num(h,"pid")&&Num(e,"creationTime")==Num(h,"creationTime"),"the supervisor observed the same process the host reported from");
        Check(!Bool(e,"forced")&&Num(e,"exitCode")==0,"normal exit observed, not forced");
    }
    static void Finalize(string dir){Check(!File.Exists(Path.Combine(dir,"complete.tsv")),"new seal only");var held=Freeze(dir,false);
        try{ValidateQueued(dir);ValidateHostAndExit(dir);ArtifactSeal.Complete(dir,RequiredFor(dir));}finally{foreach(var f in held)f.Dispose();}}
    static SessionSample[] Read(string dir){
        var held=Freeze(dir,true);try{
            var lines=File.ReadAllLines(Path.Combine(dir,"complete.tsv"));var required=RequiredFor(dir);Check(lines.Length==required.Length+1&&lines[0]=="dbce-playback-seal-v1","exact seal inventory");
            var names=lines.Skip(1).Select(x=>x.Split('\t')).ToArray();Check(names.All(x=>x.Length==3)&&
                names.Select(x=>x[0]).OrderBy(x=>x,StringComparer.Ordinal).SequenceEqual(required.OrderBy(x=>x,StringComparer.Ordinal)),"exact sealed members");
            ArtifactSeal.Verify(dir,required);ValidateHostAndExit(dir);return ValidateQueued(dir);
        }finally{foreach(var f in held)f.Dispose();}
    }
    static bool Refused(Action action,string because){string why;try{action();return false;}catch(InvalidDataException e){why=e.Message;}catch(IOException e){why=e.Message;}
        Console.WriteLine("   refused ("+because+"): "+why);return why.Contains(because,StringComparison.Ordinal);}
    static int Main(string[] args){try{
        // Reuse the exact reviewed validator for a capture from the combined
        // real-provider/recorder fixture. The export is outside the sealed bundle.
        if(args.Length==3&&args[0]=="--seal-export"){
            var dir=Path.GetFullPath(args[1]);Finalize(dir);var captured=Read(dir);
            using var output=new StreamWriter(new FileStream(args[2],FileMode.CreateNew,FileAccess.Write));
            foreach(var row in captured){output.WriteLine("ROW "+row.Channels.Count);
                foreach(var value in row.Channels.OrderBy(x=>x.Key,StringComparer.Ordinal))
                    output.WriteLine(value.Key+" "+value.Value.ToString("R",CultureInfo.InvariantCulture));}
            Console.WriteLine($"PASS validated export: {captured.Length} rows; {checks} checks");return 0;
        }
        var root=args.Single();string Capture(string mode)=>Path.Combine(root,mode,"capture");
        // The resolved run seals and reads back all its actual frames.
        Finalize(Capture("resolved"));var rows=Read(Capture("resolved"));Check(rows.Length==16,"16 actual frames sealed and read");
        // The worker evidence of the provider-stuck run is valid on its own; the seal still refuses it.
        ValidateQueued(Capture("provider-stuck"));
        Check(Refused(()=>Finalize(Capture("provider-stuck")),"host shutdown resolved"),"a valid worker report beside an unresolved host and a forced exit never seals");
        Check(!File.Exists(Path.Combine(Capture("writer-stuck"),"worker.tsv"))&&Refused(()=>Finalize(Capture("writer-stuck")),"Could not find file"),"a timed-out writer leaves no worker report and never seals");
        Check(Refused(()=>Finalize(Capture("ordinary-provider-stuck")),"host shutdown resolved"),"an ordinary forced exit never seals");
        // Targeted refusals on copies of the resolved capture (before its seal was written there).
        int copy=0;string Copy(){var d=Path.Combine(root,"neg-"+(copy++));Directory.CreateDirectory(d);foreach(var f in Required)File.Copy(Path.Combine(Capture("resolved"),f),Path.Combine(d,f));return d;}
        void Json(string d,string file,string from,string to){var p=Path.Combine(d,file);var t=File.ReadAllText(p);Check(t.Contains(from),"mutation anchor "+from);File.WriteAllText(p,t.Replace(from,to));}
        {var d=Copy();Json(d,"host-shutdown.json","\"state\":\"resolved\"","\"state\":\"unresolved\"");Check(Refused(()=>Finalize(d),"host shutdown resolved"),"host unresolved refused");}
        {var d=Copy();Json(d,"host-shutdown.json","\"recorderJoined\":true","\"recorderJoined\":false");Check(Refused(()=>Finalize(d),"worker joined"),"recorder not joined refused");}
        {var d=Copy();var n=File.ReadAllLines(Path.Combine(d,"worker.tsv"))[0].Split('\t')[8];Json(d,"host-shutdown.json",n,new string('0',32));Check(Refused(()=>Finalize(d),"names this capture"),"host report from another capture refused");}
        {var d=Copy();Json(d,"exit.json","\"forced\":false","\"forced\":true");Check(Refused(()=>Finalize(d),"not forced"),"forced exit refused");}
        {var d=Copy();Json(d,"exit.json","\"exitCode\":0","\"exitCode\":1");Check(Refused(()=>Finalize(d),"not forced"),"nonzero exit refused");}
        {var d=Copy();var t=File.ReadAllText(Path.Combine(d,"exit.json"));var k=t.IndexOf("\"pid\":")+6;var pid=t.Substring(k,t.IndexOf(',',k)-k);
            Json(d,"exit.json","\"pid\":"+pid,"\"pid\":"+(ulong.Parse(pid)+1));Check(Refused(()=>Finalize(d),"same process"),"another process's exit refused");}
        {var d=Copy();File.Delete(Path.Combine(d,"exit.json"));Check(Refused(()=>Finalize(d),"exit.json"),"missing exit observation refused");}
        {var d=Copy();File.Delete(Path.Combine(d,"host-shutdown.json"));Check(Refused(()=>Finalize(d),"host-shutdown.json"),"missing host report refused");}
        {var d=Copy();Finalize(d);Json(d,"exit.json","\"exitCode\":0","\"exitCode\":0 ");Check(Refused(()=>Read(d),"Capture changed"),"a sealed exit observation cannot change");}
        // Identity controls (Astra 12080): matching zero identities, and missing identity fields.
        void Node(string d,string file,Action<System.Text.Json.Nodes.JsonObject> edit){var p=Path.Combine(d,file);var o=System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(p))!.AsObject();edit(o);File.WriteAllText(p,o.ToJsonString());}
        {var d=Copy();foreach(var f in new[]{"host-shutdown.json","exit.json"})Node(d,f,o=>{o["pid"]=0;o["creationTime"]=0;});Check(Refused(()=>Finalize(d),"nonzero process identity"),"matching zero identities refused");}
        {var d=Copy();Node(d,"exit.json",o=>o.Remove("creationTime"));Check(Refused(()=>Finalize(d),"missing creationTime"),"missing exit identity refused");}
        {var d=Copy();Node(d,"host-shutdown.json",o=>o.Remove("pid"));Check(Refused(()=>Finalize(d),"missing pid"),"missing host identity refused");}
        {var d=Copy();Node(d,"exit.json",o=>o["forced"]="no");Check(Refused(()=>Finalize(d),"wrong type: forced"),"mistyped exit field refused");}
        Console.WriteLine($"PASS P21 finalizer: resolved capture sealed and read ({rows.Length} actual frames); 3 unresolved captures and 13 targeted mutations refused; {checks} checks");
        return 0;
    }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
