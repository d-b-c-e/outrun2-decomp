using System;
using System.IO;
using System.Linq;
using Dbce.Wheel.Playback;

static class BundleChecks {
    static int checks,serial;
    static string capture,output;
    static string[] outcomes;
    static void Check(bool ok,string why){checks++;if(!ok)throw new Exception(why);}
    static void Refuse(Action action,string why){
        bool refused=false;try{action();}catch(InvalidDataException){refused=true;}catch(IOException){refused=true;}catch(OverflowException){refused=true;}
        Check(refused,why);
    }
    static string Copy(string mode="normal"){
        var dir=Path.Combine(output,(++serial).ToString("D3")+"-"+mode);
        if(Directory.Exists(dir))throw new IOException("New fixture directory required");
        Directory.CreateDirectory(dir);
        File.Copy(Path.Combine(capture,mode+".jsonl"),Path.Combine(dir,"source.jsonl"));
        File.WriteAllText(Path.Combine(dir,"worker.tsv"),outcomes.Single(x=>x.StartsWith(mode+"\t",StringComparison.Ordinal))+"\n");
        File.WriteAllText(Path.Combine(dir,"bundle.meta"),CompletionBundle.Contract);
        return dir;
    }
    static void Field(string dir,int index,string value){
        var file=Path.Combine(dir,"worker.tsv");var p=File.ReadAllText(file).TrimEnd('\r','\n').Split('\t');
        p[index]=value;File.WriteAllText(file,string.Join("\t",p)+"\n");
    }
    public static void Run(string root){
        capture=root;output=Path.Combine(root,"bundle-proof");
        if(Directory.Exists(output))throw new IOException("Fresh bundle proof directory required");
        Directory.CreateDirectory(output);outcomes=File.ReadAllLines(Path.Combine(root,"outcomes.tsv"));
        var normal=Copy();
        Refuse(()=>CompletionBundle.Read(normal),"A complete inner stream without outer seal is refused");
        CompletionBundle.Finalize(normal);
        Check(CompletionBundle.Read(normal).Length==2,"Native worker completion and exact rows survive the production seal/reader");
        var seal=File.ReadAllBytes(Path.Combine(normal,"complete.tsv"));
        Refuse(()=>CompletionBundle.Finalize(normal),"Completed bundle never overwritten");
        Check(seal.SequenceEqual(File.ReadAllBytes(Path.Combine(normal,"complete.tsv"))),"Existing seal retained exactly");
        foreach(var mode in new[]{"abandoned","limited","late"}){
            var dir=Copy(mode);Refuse(()=>CompletionBundle.Finalize(dir),"Failed native worker cannot finalize: "+mode);
            Check(!File.Exists(Path.Combine(dir,"complete.tsv")),"Failure leaves no completion seal: "+mode);
            ArtifactSeal.Complete(dir,CompletionBundle.Required);
            Refuse(()=>CompletionBundle.Read(dir),"Generic valid seal cannot upgrade an ineligible capture: "+mode);
        }
        foreach(var name in CompletionBundle.Required){
            var dir=Copy();CompletionBundle.Finalize(dir);File.AppendAllText(Path.Combine(dir,name)," ");
            Refuse(()=>CompletionBundle.Read(dir),"Sealed member tamper refused: "+name);
        }
        {var dir=Copy();CompletionBundle.Finalize(dir);var file=Path.Combine(dir,"source.jsonl");
         File.WriteAllText(file,File.ReadAllText(file).Replace("spike-only","spike-test"));
         Refuse(()=>CompletionBundle.Read(dir),"Changed but still valid JSON metadata is refused by file hash");}
        {var dir=Copy();CompletionBundle.Finalize(dir);Field(dir,14,"1");Field(dir,10,"3");
         Refuse(()=>CompletionBundle.Read(dir),"Changed but semantically valid worker report is refused by seal hash");}
        {var dir=Copy();File.Copy(Path.Combine(capture,"late.jsonl"),Path.Combine(dir,"source.jsonl"),true);
         Refuse(()=>CompletionBundle.Finalize(dir),"Eligible report cannot seal another run's late-complete source");
         Check(!File.Exists(Path.Combine(dir,"complete.tsv")),"Swapped attempt gets no seal");
         ArtifactSeal.Complete(dir,CompletionBundle.Required);
         Refuse(()=>CompletionBundle.Read(dir),"Generic seal cannot validate a swapped report/source pair");}
        {var dir=Copy();File.Copy(Path.Combine(capture,"late.jsonl"),Path.Combine(dir,"source.jsonl"),true);
         Field(dir,21,new FileInfo(Path.Combine(dir,"source.jsonl")).Length.ToString());Field(dir,22,ArtifactSeal.Hash(Path.Combine(dir,"source.jsonl")));
         Refuse(()=>CompletionBundle.Finalize(dir),"Correct file hash with a different attempt nonce still refuses");}
        {var dir=Copy();Field(dir,22,new string('0',64));
         Refuse(()=>CompletionBundle.Finalize(dir),"Correct nonce with an incorrect worker source hash refuses");}
        {var dir=Copy();Field(dir,21,"1");Refuse(()=>CompletionBundle.Finalize(dir),"Incorrect worker source length refuses");}
        {var dir=Copy();Field(dir,15,"1");Field(dir,10,"3");ArtifactSeal.Complete(dir,CompletionBundle.Required);
         Refuse(()=>CompletionBundle.Read(dir),"Pre-end foreign producer loss refuses even with reconciled counts");}
        {var dir=Copy();Field(dir,23,"1");Field(dir,10,"3");CompletionBundle.Finalize(dir);
         Check(CompletionBundle.Read(dir).Length==2,"Foreign post-end diagnostics are distinct from capture loss");}
        {var dir=Copy();File.WriteAllText(Path.Combine(dir,"unexpected.bin"),"extra");ArtifactSeal.Complete(dir,CompletionBundle.Required.Concat(new[]{"unexpected.bin"}).ToArray());
         Refuse(()=>CompletionBundle.Read(dir),"Closed fixture contract refuses extra sealed members");}
        foreach(var (column,value) in new[]{(1,"999"),(2,"0"),(3,"3"),(4,"1"),(5,"durationLimit"),(6,"0"),(7,"0"),(8,"0"),(9,"1"),
            (10,"3"),(11,"1"),(12,"1"),(13,"1"),(16,"1"),(17,"1"),(18,"1"),(19,"1"),(10,"-1"),(10,"1.0")}){
            var dir=Copy();Field(dir,column,value);ArtifactSeal.Complete(dir,CompletionBundle.Required);
            Refuse(()=>CompletionBundle.Read(dir),"Re-sealed invalid report refused: "+column+"="+value);
        }
        {var dir=Copy();Field(dir,14,"1");Field(dir,10,"3");CompletionBundle.Finalize(dir);
         Check(CompletionBundle.Read(dir).Length==2,"Counted post-end diagnostics do not invent capture loss");}
        {var dir=Copy();File.AppendAllText(Path.Combine(dir,"worker.tsv"),"second report\n");ArtifactSeal.Complete(dir,CompletionBundle.Required);
         Refuse(()=>CompletionBundle.Read(dir),"Multiple worker reports refused");}
        {var dir=Copy();File.WriteAllText(Path.Combine(dir,"bundle.meta"),"dbce.trajectory@1\n");ArtifactSeal.Complete(dir,CompletionBundle.Required);
         Refuse(()=>CompletionBundle.Read(dir),"No trajectory or unsupported contract adoption");}
        {var dir=Copy();ArtifactSeal.Complete(dir,new[]{"source.jsonl","worker.tsv"});
         Refuse(()=>CompletionBundle.Read(dir),"Unsealed required metadata refused");}
        {var dir=Copy();ArtifactSeal.Complete(dir,CompletionBundle.Required.Concat(new[]{"source.jsonl"}).ToArray());
         Refuse(()=>CompletionBundle.Read(dir),"Duplicate sealed member refused");}
        {var dir=Copy();File.WriteAllText(Path.Combine(dir,"complete.tsv"),"dbce-playback-seal-v1\n../source.jsonl\t0\tbad\n");
         Refuse(()=>CompletionBundle.Read(dir),"Parent traversal in seal refused");}
        {var dir=Copy();File.AppendAllText(Path.Combine(dir,"source.jsonl"),"{}\n");ArtifactSeal.Complete(dir,CompletionBundle.Required);
         Refuse(()=>CompletionBundle.Read(dir),"Even re-sealed data after footer is refused by production reader");}
        {var dir=Copy();var file=Path.Combine(dir,"source.jsonl");File.WriteAllText(file,File.ReadAllText(file).Replace("\"source.seq.lo\":2","\"source.seq.lo\":3"));
         ArtifactSeal.Complete(dir,CompletionBundle.Required);Refuse(()=>CompletionBundle.Read(dir),"Re-sealed source sequence gap refused");}
        {var dir=Copy();File.WriteAllText(Path.Combine(dir,"complete.tsv.tmp"),"existing temporary file");
         Refuse(()=>CompletionBundle.Finalize(dir),"Seal publication refuses existing temporary file");
         Check(!File.Exists(Path.Combine(dir,"complete.tsv"))&&File.ReadAllText(Path.Combine(dir,"complete.tsv.tmp"))=="existing temporary file","Publication failure is not completion and preserves conflicting file");}
        {var dir=Copy();using(var file=new FileStream(Path.Combine(dir,"source.jsonl"),FileMode.Open,FileAccess.Write,FileShare.ReadWrite))
             Refuse(()=>CompletionBundle.Finalize(dir),"Open writer prevents finalization");
         Check(!File.Exists(Path.Combine(dir,"complete.tsv")),"No seal while source is writable");}
        Console.WriteLine("PASS completion bundle: "+checks+" checks; production ArtifactSeal + SessionReader, fixture contract only");
    }
}
