// Fixture-only admission and numeric export. Does not claim portable game replay.
using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using Dbce.Wheel.Playback;
using Dbce.Wheel.Recording;

static class Program {
    static readonly string[] Required={"bundle.meta","profile.ini","worker.tsv","source.jsonl","budget.tsv"};
    const string Contract="dbce.shim.queued-frames-spike@2\n", InputContract="shim-frame-audit-2-pending-unadopted";
    static int checks;
    static void Check(bool ok,string why){++checks;if(!ok)throw new InvalidDataException(why);}
    static ulong U(string s){Check(ulong.TryParse(s,NumberStyles.None,CultureInfo.InvariantCulture,out var n),"unsigned decimal");return n;}
    static uint Limb(double n){Check(double.IsFinite(n)&&n>=0&&n<=uint.MaxValue&&n==Math.Floor(n),"uint32 limb");return (uint)n;}
    static ulong Wide(IDictionary<string,double> c,string k)=>Limb(c[k+".lo"])|((ulong)Limb(c[k+".hi"])<<32);
    static List<FileStream> Freeze(string dir,bool sealedBundle){
        var held=new List<FileStream>();try{
            foreach(var name in Required.Concat(sealedBundle?new[]{"complete.tsv"}:Array.Empty<string>())){
                var path=Path.GetFullPath(Path.Combine(dir,name));
                for(var p=path;p!=null;p=Path.GetDirectoryName(p))Check((File.GetAttributes(p)&FileAttributes.ReparsePoint)==0,"reparse path");
                var f=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read);held.Add(f);
                Check(f.Length<=(name=="source.jsonl"?1024*1024:65536),"fixture file bound");
            }return held;
        }catch{foreach(var f in held)f.Dispose();throw;}
    }
    static SessionSample[] Validate(string dir){
        Check(File.ReadAllText(Path.Combine(dir,"bundle.meta"))==Contract,"known fixture contract");
        var lines=File.ReadAllLines(Path.Combine(dir,"worker.tsv"));Check(lines.Length==1,"one worker report");
        var p=lines[0].Split('\t');Check(p.Length==23,"known report layout");
        Check(p[0]=="2"&&p[11]=="1"&&p[12]=="1"&&p[22]=="stopped","worker completed and quiescent");
        for(int i=13;i<=21;++i)Check(p[i]=="0","no timeout or loss or diagnostic call in this fixture");
        var accepted=U(p[1]);var written=U(p[2]);var attempted=U(p[3]);var tail=U(p[4]);var last=U(p[5]);var first=U(p[6]);var origin=U(p[7]);
        Check(first>0&&tail>=first&&tail!=ulong.MaxValue&&accepted>0&&accepted==written&&attempted==accepted&&tail==last&&written==tail-first+1,"first/tail/count accounting");
        var records=SessionReader.Read(Path.Combine(dir,"source.jsonl")).ToArray();
        var m=records[0].Metadata;var footer=records.Last().Footer;
        Check(m.Game=="outrun-queued-frames-fixture"&&m.Properties["inputContract"]==InputContract,"known frame contract");
        Check(p[8].Length==32&&p[8].All(c=>"0123456789abcdef".Contains(c))&&m.Properties["captureNonce"]==p[8],"same capture nonce");
        Check((ulong)new FileInfo(Path.Combine(dir,"source.jsonl")).Length==U(p[9])&&ArtifactSeal.Hash(Path.Combine(dir,"source.jsonl"))==p[10],"worker-closed source bytes");
        Check(m.Properties["profileSha256"]==ArtifactSeal.Hash(Path.Combine(dir,"profile.ini")),"exact profile bytes");
        var count=footer.Counts;
        Check(footer.Completed&&footer.StopReason=="stopped"&&count.AcceptedSamples==(long)accepted&&count.WrittenSamples==(long)written&&
            count.DroppedSamples==0&&count.DroppedMarkers==0&&count.QueueFullCount==0&&count.ContentionCount==0&&count.InvalidCount==0&&
            count.LimitCount==0&&count.ErrorCount==0&&count.AcceptedMarkers==0&&count.WrittenMarkers==0,"inner worker accounting");
        Check(records.All(x=>x.Marker==null),"fixture has no event journal");
        var rows=records.Where(x=>x.Sample!=null).Select(x=>x.Sample).ToArray();Check((ulong)rows.Length==written,"complete rows");
        var names=rows[0].Channels.Keys.OrderBy(x=>x,StringComparer.Ordinal).ToArray();Check(names.Length==382,"complete two-device plus adapter schema and inputs");
        for(int i=0;i<rows.Length;++i){var row=rows[i];var c=row.Channels;var seq=Wide(c,"frame");var tick=Wide(c,"tickUs");
            Check(c.Keys.OrderBy(x=>x,StringComparer.Ordinal).SequenceEqual(names),"fixed complete schema");
            Check(seq==first+(ulong)i&&Wide(c,"adapter.frame")==seq&&tick>=origin&&row.ElapsedSeconds==(tick-origin)/1e6,"source sequence/time/adapter join");
            for(int device=0;device<2;++device)foreach(var call in new[]{"poll","state"}){
                var v=c[$"sample.{device}.{call}"];Check(v>=0&&v<=4&&v==Math.Floor(v),"known Call enum including Pending");
            }
        }
        var budget=File.ReadAllText(Path.Combine(dir,"budget.tsv")).TrimEnd('\r','\n').Split('\t');
        Check(budget.Length==3&&U(budget[0])==382&&U(budget[2])<=4*1024*1024,"declared schema and queue bytes");
        // Native preflight counts exact ASCII key escaping + 32-byte finite binary64 + fixed JSON reserve.
        var bound=192UL+names.Aggregate(0UL,(sum,n)=>sum+(ulong)System.Text.Json.JsonSerializer.Serialize(n).Length+34UL);
        Check(U(budget[1])==bound,"independent worst-case row bound");
        var data=File.ReadAllLines(Path.Combine(dir,"source.jsonl"));
        Check(Encoding.UTF8.GetByteCount(data[0])+1<=8192,"fixture metadata reserve");
        Check(data.Skip(1).Take(rows.Length).All(x=>(ulong)Encoding.UTF8.GetByteCount(x)+1<=bound),"every actual row fits declared bound");
        return rows;
    }
    static SessionSample[] Read(string dir){
        var held=Freeze(dir,true);try{
            var lines=File.ReadAllLines(Path.Combine(dir,"complete.tsv"));Check(lines.Length==Required.Length+1&&lines[0]=="dbce-playback-seal-v1","exact seal inventory");
            var names=lines.Skip(1).Select(x=>x.Split('\t')).ToArray();Check(names.All(x=>x.Length==3)&&
                names.Select(x=>x[0]).OrderBy(x=>x,StringComparer.Ordinal).SequenceEqual(Required.OrderBy(x=>x,StringComparer.Ordinal)),"exact sealed members");
            ArtifactSeal.Verify(dir,Required);return Validate(dir);
        }finally{foreach(var f in held)f.Dispose();}
    }
    static void Finalize(string dir){Check(!File.Exists(Path.Combine(dir,"complete.tsv")),"new seal only");var held=Freeze(dir,false);
        try{Validate(dir);ArtifactSeal.Complete(dir,Required);}finally{foreach(var f in held)f.Dispose();}}
    static void Refuse(Action action,string why){bool no=false;try{action();}catch(InvalidDataException){no=true;}catch(IOException){no=true;}
        Check(no,why);}
    static void NegativeControls(string root){
        var full=Path.Combine(root,"full");int copy=0;
        string Copy(){var dir=Path.Combine(root,"bad-"+(copy++));Directory.CreateDirectory(dir);foreach(var f in Required)File.Copy(Path.Combine(full,f),Path.Combine(dir,f));return dir;}
        void Field(string dir,int index,string value){var p=File.ReadAllText(Path.Combine(dir,"worker.tsv")).TrimEnd('\r','\n').Split('\t');p[index]=value;File.WriteAllText(Path.Combine(dir,"worker.tsv"),string.Join("\t",p)+"\n");}
        {var d=Copy();File.Copy(Path.Combine(root,"window","source.jsonl"),Path.Combine(d,"source.jsonl"),true);Refuse(()=>Finalize(d),"swapped source refused");}
        {var d=Copy();Field(d,6,"2");Refuse(()=>Finalize(d),"first sequence mismatch refused");}
        {var d=Copy();Field(d,4,"15");Field(d,5,"15");Refuse(()=>Finalize(d),"last sequence/count mismatch refused");}
        {var d=Copy();Field(d,13,"1");Refuse(()=>Finalize(d),"timeout with good footer refused");}
        {var d=Copy();Field(d,14,"1");Refuse(()=>Finalize(d),"upstream drop with good footer refused");}
        {var d=Copy();File.AppendAllText(Path.Combine(d,"profile.ini"),"; changed\n");Refuse(()=>Finalize(d),"changed exact profile refused");}
        {var d=Copy();Finalize(d);File.WriteAllText(Path.Combine(d,"extra"),"extra");File.Delete(Path.Combine(d,"complete.tsv"));ArtifactSeal.Complete(d,Required.Concat(new[]{"extra"}).ToArray());Refuse(()=>Read(d),"extra sealed member refused");}
        {var d=Copy();Finalize(d);Field(d,7,"0");Refuse(()=>Read(d),"changed worker report refused");}
        Console.WriteLine("PASS queued bundle: 8 targeted refusal controls");
    }
    static void Export(string dir,SessionSample[] rows){using var w=new StreamWriter(new FileStream(Path.Combine(dir,"frames.tsv"),FileMode.CreateNew));
        foreach(var r in rows){w.WriteLine("ROW "+r.Channels.Count);foreach(var c in r.Channels.OrderBy(x=>x.Key,StringComparer.Ordinal))w.WriteLine(c.Key+" "+c.Value.ToString("R",CultureInfo.InvariantCulture));}}
    static int Main(string[] args){try{
        var root=args.Single();foreach(var name in new[]{"full","window"}){var dir=Path.Combine(root,name);Finalize(dir);var rows=Read(dir);Export(dir,rows);}
        NegativeControls(root);Console.WriteLine($"PASS production Reader/ArtifactSeal: {checks} checks, all rows validated before export");return 0;
    }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
