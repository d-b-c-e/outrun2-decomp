// Offline completion-envelope proof. This contract deliberately cannot claim input or trajectory replay.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using Dbce.Wheel.Playback;
using Dbce.Wheel.Recording;

static class CompletionBundle {
    public const string Contract="dbce.shim.worker-bundle-spike@2\n";
    public static readonly string[] Required={"bundle.meta","worker.tsv","source.jsonl"};
    static void Require(bool ok,string why){if(!ok)throw new InvalidDataException(why);}
    // Files are in a new, recorder-owned directory. Deny ordinary writes/deletes for the whole validation/seal step.
    // Reparse rejection is not a claim of resisting a hostile process replacing parent directories concurrently.
    static List<FileStream> Freeze(string directory,bool sealedBundle){
        var held=new List<FileStream>();
        try{
            foreach(var name in Required.Concat(sealedBundle?new[]{"complete.tsv"}:Array.Empty<string>())){
                var path=Path.GetFullPath(Path.Combine(directory,name));
                for(string p=path;p!=null;p=Path.GetDirectoryName(p))
                    Require((File.GetAttributes(p)&FileAttributes.ReparsePoint)==0,"Reparse path refused");
                var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read);
                held.Add(stream);
                Require(stream.Length<=(name=="source.jsonl"?1024*1024:4096),"Fixture file budget exceeded");
            }
            return held;
        }catch{foreach(var file in held)file.Dispose();throw;}
    }
    static ulong Natural(string text){
        Require(ulong.TryParse(text,NumberStyles.None,CultureInfo.InvariantCulture,out var value),"Invalid counter");
        return value;
    }
    static ulong Limb(SessionSample sample,string name){
        Require(sample.Channels.TryGetValue(name,out var value)&&value>=0&&value<=uint.MaxValue&&value==Math.Truncate(value),"Invalid integer limb");
        return (ulong)value;
    }
    static ulong Wide(SessionSample sample,string name)=>Limb(sample,name+".lo")|(Limb(sample,name+".hi")<<32);
    static void BindSource(string directory,string[] report,SessionMetadata metadata){
        var nonce=report[20];var size=Natural(report[21]);var hash=report[22];
        Require(nonce.Length==32&&nonce.All(c=>"0123456789abcdef".Contains(c))&&
            metadata.Properties.TryGetValue("captureNonce",out var recorded)&&recorded==nonce,"Worker report nonce differs from stream");
        var path=Path.Combine(directory,"source.jsonl");
        Require(size>0&&(ulong)new FileInfo(path).Length==size&&hash.Length==64&&hash==ArtifactSeal.Hash(path),"Worker report does not vouch for these bytes");
    }
    static void ExactInventory(string directory){
        var lines=File.ReadAllLines(Path.Combine(directory,"complete.tsv"));
        Require(lines.Length==Required.Length+1&&lines[0]=="dbce-playback-seal-v1","Unexpected seal inventory");
        var entries=lines.Skip(1).Select(x=>x.Split('\t')).ToArray();
        Require(entries.All(x=>x.Length==3)&&entries.Select(x=>x[0]).OrderBy(x=>x,StringComparer.Ordinal)
            .SequenceEqual(Required.OrderBy(x=>x,StringComparer.Ordinal)),"Unexpected sealed members");
    }
    static SessionSample[] Validate(string directory){
        Require(File.ReadAllText(Path.Combine(directory,"bundle.meta"))==Contract,"Unsupported bundle contract");
        var lines=File.ReadAllLines(Path.Combine(directory,"worker.tsv"));
        Require(lines.Length==1,"Expected one quiescent worker report");
        var p=lines[0].Split('\t');Require(p.Length==24,"Unknown worker report revision");
        // Prototype enum Complete=2; other revisions require a new contract, not permissive interpretation.
        Require(p[1]=="2"&&p[2]=="1"&&p[5]=="stopped","Worker was not eligible to seal");
        Require(p[6]=="1"&&p[7]=="1"&&p[8]=="1"&&p[9]=="0","Worker/producer not quiescent or drain timed out");
        Require(p[18]=="0","Ingress loss was recorded");
        var accepted=Natural(p[3]);var written=Natural(p[4]);var attempted=Natural(p[10]);
        var queueFull=Natural(p[11]);var invalid=Natural(p[12]);var afterFailure=Natural(p[13]);
        var afterEnd=Natural(p[14]);var wrongProducer=Natural(p[15]);
        var afterEndWrongProducer=Natural(p[23]);
        var declared=Natural(p[16]);var lastWritten=Natural(p[17]);var discarded=Natural(p[19]);
        Require(accepted>0&&accepted==written&&written==declared&&declared==lastWritten,"Source tail/count mismatch");
        Require(queueFull==0&&invalid==0&&afterFailure==0&&discarded==0&&wrongProducer==0,"Lossy capture");
        Require(attempted==checked(accepted+queueFull+invalid+afterFailure+afterEnd+wrongProducer+afterEndWrongProducer),"Attempt accounting mismatch");
        // Enumerate completely before exposing any rows; SessionReader validates ordering, footer and all records.
        var records=SessionReader.Read(Path.Combine(directory,"source.jsonl")).ToArray();
        Require(records[0].Metadata.Game=="outrun-recorder-lifecycle-fixture","Not the known native fixture");
        BindSource(directory,p,records[0].Metadata);
        Require(records.All(x=>x.Marker==null),"Unknown fixture markers");
        var footer=records.Last().Footer;var counts=footer.Counts;
        Require(footer.Completed&&footer.StopReason=="stopped","Incomplete inner stream");
        Require(counts.AcceptedSamples==(long)accepted&&counts.WrittenSamples==(long)written&&counts.DroppedSamples==0&&
            counts.DroppedMarkers==0&&counts.AcceptedMarkers==0&&counts.WrittenMarkers==0&&counts.QueueFullCount==0&&
            counts.ContentionCount==0&&counts.InvalidCount==0&&counts.LimitCount==0&&counts.ErrorCount==0,"Inner/outer accounting differs");
        var rows=records.Where(x=>x.Sample!=null).Select(x=>x.Sample).ToArray();
        Require((ulong)rows.Length==written,"Inner sample count differs");
        for(int i=0;i<rows.Length;i++){
            var row=rows[i];var seq=Wide(row,"source.seq");var time=Wide(row,"source.us");
            Require(row.Channels.Count==6&&seq==(ulong)i+1&&time==seq*100000&&row.ElapsedSeconds==time/1000000.0,"Fixture source sequence/time differs");
            Require(row.Channels.TryGetValue("value.0",out var positive)&&row.Channels.TryGetValue("value.1",out var negative)&&
                positive==seq&&negative==-(double)seq,"Fixture snapshot differs");
        }
        return rows;
    }
    public static void Finalize(string directory){
        Require(!File.Exists(Path.Combine(directory,"complete.tsv")),"Existing seal cannot be replaced");
        var held=Freeze(directory,false);
        try{Validate(directory);ArtifactSeal.Complete(directory,Required);}
        finally{foreach(var file in held)file.Dispose();}
    }
    public static SessionSample[] Read(string directory){
        var held=Freeze(directory,true);
        try{ExactInventory(directory);ArtifactSeal.Verify(directory,Required);return Validate(directory);}
        finally{foreach(var file in held)file.Dispose();}
    }
}
