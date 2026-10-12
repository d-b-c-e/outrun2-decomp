// A stopped recorder can save while its host is alive. This checkpoint never asserts a normal process exit;
// P21's separate complete.tsv still requires host-shutdown.json and the supervisor's exit.json.
using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Dbce.Wheel.Playback;
using Dbce.Wheel.Recording;

static partial class Program {
    const string RecordingContract="dbce.shim.outrun-capture@2\n";
    const string RecordingReport="recorder-complete.json",RecordingSeal="recording-complete.tsv";
    static readonly string[] RecordingMembers=Queued.Concat(new[]{"devices.json"}).OrderBy(x=>x,StringComparer.Ordinal).ToArray();
    static readonly string[] RecordingRequired=RecordingMembers.Concat(new[]{RecordingReport}).ToArray();
    static readonly string[] FinalChannels=("steering accelerator brake shiftUp shiftDown menuLeft menuRight menuUp menuDown menuConfirm menuCancel start featureMask deviceHeld frontAxis0 frontAxis1 frontAxis2 frontAxis3 connected pcInputLayer pcRecordUsed pcOverlay pcOverlayYield pcButtons pcSteer pcAccel pcBrake")
        .Split(' ').Select(x=>"final."+x).Concat("enter esc up down left right any drives".Split(' ').Select(x=>"keyboard."+x)).ToArray();
    static bool RecordingV2(string dir)=>File.ReadAllText(Path.Combine(dir,"bundle.meta"))==RecordingContract;
    static bool Hex(string s,int length)=>s.Length==length&&s.All(c=>"0123456789abcdef".Contains(c));
    static string TextHash(string s)=>Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(s))).ToLowerInvariant();

    static void VerifyRecordingSeal(string dir){
        var lines=File.ReadAllLines(Path.Combine(dir,RecordingSeal));
        Check(lines.Length==RecordingRequired.Length+1&&lines[0]=="dbce-playback-seal-v1","exact recording seal inventory");
        var entries=lines.Skip(1).Select(x=>x.Split('\t')).ToArray();
        Check(entries.All(x=>x.Length==3)&&entries.Select(x=>x[0]).OrderBy(x=>x,StringComparer.Ordinal)
            .SequenceEqual(RecordingRequired.OrderBy(x=>x,StringComparer.Ordinal)),"exact recording sealed members");
        ArtifactSeal.VerifyNamed(dir,RecordingSeal,RecordingRequired);
    }

    static string ValidateRecordingReport(string dir,string request,string exe,ulong pid,ulong creation){
        Check(RecordingV2(dir),"ordinary recording contract 2 required");
        Check(Hex(request,32)&&Hex(exe,64)&&pid>0&&creation>0,"expected request and process identity");
        using var document=JsonDocument.Parse(File.ReadAllText(Path.Combine(dir,RecordingReport)));
        var r=document.RootElement;
        Check(Str(r,"schema")=="outrun.recorder-complete@1"&&Str(r,"contract")==RecordingContract.TrimEnd('\n'),"known recorder completion report");
        Check(Str(r,"requestId")==request&&Str(r,"exeSha256")==exe&&Num(r,"processId")==pid&&Num(r,"processCreationFileTime")==creation,"completion matches expected request and process");
        Check(Str(r,"outcome")=="Complete"&&Bool(r,"joined"),"recorder complete and joined");
        var action=r.TryGetProperty("action",out var actionValue)?actionValue.GetString():"record";
        Check(action is "record" or "playback","known recording action");
        var stoppedBy=Str(r,"stoppedBy");
        Check(new[]{"f11","stop-request","limit","exit"}.Contains(stoppedBy)||
            (stoppedBy=="tape-end"&&action=="playback"),"known intentional recording stop for this action");
        Check(DateTimeOffset.TryParse(Str(r,"startedUtc"),CultureInfo.InvariantCulture,DateTimeStyles.None,out var start)&&
            DateTimeOffset.TryParse(Str(r,"completedUtc"),CultureInfo.InvariantCulture,DateTimeStyles.None,out var end)&&
            start.Offset==TimeSpan.Zero&&end.Offset==TimeSpan.Zero&&end>=start,"ordered UTC recording times");
        var m=SessionReader.Read(Path.Combine(dir,"source.jsonl")).First().Metadata;
        Check(m.Properties.TryGetValue("runNonce",out var nonce)&&nonce==request,"recorded request identity");
        Check(Str(r,"profileSha256")==ArtifactSeal.Hash(Path.Combine(dir,"profile.ini")),"completion profile hash");
        var p=File.ReadAllLines(Path.Combine(dir,"worker.tsv"))[0].Split('\t');
        var w=Prop(r,"report",JsonValueKind.Object);
        foreach(var item in new[]{("attempted",3),("accepted",1),("written",2),("declaredLast",4),("lastWritten",5)})
            Check(Num(w,item.Item1)==U(p[item.Item2]),"completion worker count: "+item.Item1);
        foreach(var name in new[]{"queueFull","invalid","afterFailure","afterEnd","wrongProducer","discarded"})
            Check(Num(w,name)==0,"completion has no loss: "+name);
        Check(Bool(w,"captureCountsFinal")&&Bool(w,"producerDone")&&Bool(w,"workerDone")&&!Bool(w,"drainTimedOut")&&!Bool(w,"firstRejected"),"completion quiescent without rejection or timeout");
        Check(Num(r,"pushed")==U(p[2])&&Num(r,"missedFinal")==0,"complete final input coverage");
        var budget=Prop(r,"budget",JsonValueKind.Object);
        var hz=Num(budget,"hz");var seconds=Num(budget,"seconds");var maxBytes=Num(budget,"maxBytes");var slots=Num(budget,"slots");
        Check(hz==60&&seconds>0&&seconds<=86400&&slots>0&&slots<=512&&maxBytes>0&&maxBytes<=1024UL*1024*1024,"bounded recording budget");
        Check(U(p[2])<=hz*seconds&&(ulong)new FileInfo(Path.Combine(dir,"source.jsonl")).Length<=maxBytes,"capture fits recording budget");
        var members=Prop(r,"members",JsonValueKind.Array).EnumerateArray().ToArray();
        Check(members.Length==RecordingMembers.Length&&members.Select(x=>Str(x,"name")).SequenceEqual(RecordingMembers),"exact sorted completion members");
        var manifest=new StringBuilder();
        foreach(var member in members){
            var name=Str(member,"name");var bytes=Num(member,"bytes");var hash=Str(member,"sha256");var path=Path.Combine(dir,name);
            Check(bytes==(ulong)new FileInfo(path).Length&&Hex(hash,64)&&hash==ArtifactSeal.Hash(path),"completion member bytes: "+name);
            manifest.Append(name).Append('\t').Append(bytes.ToString(CultureInfo.InvariantCulture)).Append('\t').Append(hash).Append('\n');
        }
        var artifact=TextHash(manifest.ToString());Check(Str(r,"artifactSha256")==artifact,"completion artifact digest");return artifact;
    }

    static void RecordingCommand(bool seal,string dir,string request,string exe,ulong pid,ulong creation,string receipt,bool export=false){
        // Receipt is not a capture member. Refuse an output inside the sealed directory before writing any seal.
        var root=Path.GetFullPath(dir).TrimEnd(Path.DirectorySeparatorChar)+Path.DirectorySeparatorChar;
        var output=Path.GetFullPath(receipt);
        Check(!output.StartsWith(root,StringComparison.OrdinalIgnoreCase)&&!File.Exists(output),"new receipt outside capture required");
        // An output directory alias must not put the export back inside its sealed input.
        if(export)for(var p=Path.GetDirectoryName(output);p!=null;p=Path.GetDirectoryName(p))
            Check((File.GetAttributes(p)&FileAttributes.ReparsePoint)==0,"export output reparse path");
        if(seal)Check(!File.Exists(Path.Combine(dir,RecordingSeal)),"new recording seal only");
        var held=FreezeMembers(dir,RecordingRequired.Concat(seal?Array.Empty<string>():new[]{RecordingSeal}));
        try{
            if(!seal)VerifyRecordingSeal(dir);
            var rows=ValidateQueued(dir);
            var artifact=ValidateRecordingReport(dir,request,exe,pid,creation);
            if(seal){ArtifactSeal.CompleteNamed(dir,RecordingSeal,RecordingRequired);VerifyRecordingSeal(dir);}
            if(export){
                WriteExport(rows,output);
                Console.WriteLine($"PASS recording export: {rows.Length} rows; process exit not asserted; {checks} checks");return;
            }
            var result=new {schema="outrun.recording-verified@1",requestId=request,processId=pid,processCreationFileTime=creation,
                exeSha256=exe,artifactSha256=artifact,sealSha256=ArtifactSeal.Hash(Path.Combine(dir,RecordingSeal)),
                recordingSaved=true,rows=rows.Length,scope="recording saved; process exit not asserted"};
            using(var stream=new FileStream(output,FileMode.CreateNew,FileAccess.Write))JsonSerializer.Serialize(stream,result);
            Console.WriteLine($"PASS recording saved: {rows.Length} rows; process exit not asserted; {checks} checks");
        }finally{foreach(var f in held)f.Dispose();}
    }
}
