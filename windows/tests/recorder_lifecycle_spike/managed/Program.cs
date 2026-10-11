using System;
using System.IO;
using System.Linq;
using Dbce.Wheel.Recording;

static class Program {
    static void Check(bool ok,string why) { if(!ok) throw new Exception(why); }
    static void Main(string[] args) {
        var root=args.Single();
        var outcomes=File.ReadAllLines(Path.Combine(root,"outcomes.tsv")).Select(x=>x.Split('\t')).ToDictionary(x=>x[0]);
        var normal=SessionReader.Read(Path.Combine(root,"normal.jsonl")).ToArray();
        Check(normal.Last().Footer.Completed&&normal.Last().Footer.Counts.WrittenSamples==2,"native normal completion");
        Check(outcomes["normal"][2]=="1","normal outer eligibility");
        var rows=normal.Where(x=>x.Sample!=null).Select(x=>x.Sample).ToArray();
        Check(rows[0].Channels["source.seq.lo"]==1&&rows[1].Channels["source.seq.lo"]==2,"source sequence survived worker");
        Check(rows[0].ElapsedSeconds==0.1&&rows[1].ElapsedSeconds==0.2&&rows[1].Channels["value.1"]==-2,"source time and values survived worker");
        bool refused=false;
        try { SessionReader.Read(Path.Combine(root,"abandoned.jsonl")).ToArray(); }
        catch(SessionReadException e) {refused=e.Error==SessionReadError.MissingFooter;}
        Check(refused&&outcomes["abandoned"][2]=="0","overflow abandoned native file cannot qualify");
        var limited=SessionReader.Read(Path.Combine(root,"limited.jsonl")).ToArray();
        Check(!limited.Last().Footer.Completed&&outcomes["limited"][2]=="0"&&outcomes["limited"][5]=="durationLimit","writer limit reason retained, incomplete inner footer");
        var late=SessionReader.Read(Path.Combine(root,"late.jsonl")).ToArray();
        Check(late.Last().Footer.Completed&&outcomes["late"][2]=="0","complete late inner footer remains outer-ineligible");
        Console.WriteLine("PASS production reader: exact normal rows, abandoned overflow refusal, native limit reason, late inner completion cannot qualify");
    }
}
