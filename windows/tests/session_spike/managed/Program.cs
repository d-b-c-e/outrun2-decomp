using System;
using System.Globalization;
using System.IO;
using System.Linq;
using Dbce.Wheel.Recording;

static class Program {
    static void Check(bool value, string what) { if (!value) throw new Exception(what); }
    static ulong Join(System.Collections.Generic.IDictionary<string, double> c, string key) =>
        checked((uint)c[key + ".lo"]) | ((ulong)checked((uint)c[key + ".hi"]) << 32);
    static void Main(string[] args) {
        string root = args.Single();
        var frames = SessionReader.Read(Path.Combine(root, "frames.jsonl")).ToArray();
        Check(frames.Last().Footer.Completed && frames.Last().Footer.Counts.WrittenSamples == 12, "complete native frames");
        using (var output = new StreamWriter(new FileStream(Path.Combine(root, "frames.tsv"), FileMode.CreateNew))) {
            foreach (var r in frames.Where(r => r.Sample != null)) {
                output.WriteLine("ROW " + r.Sample.Channels.Count);
                foreach (var c in r.Sample.Channels.OrderBy(p => p.Key, StringComparer.Ordinal))
                    output.WriteLine(c.Key + " " + c.Value.ToString("R", CultureInfo.InvariantCulture));
            }
        }
        var ids = SessionReader.Read(Path.Combine(root, "ids.jsonl")).Single(r => r.Sample != null).Sample.Channels;
        Check((ulong)ids["unsafeDouble"] != 9007199254740993UL, "negative control: uint64 stored in one double loses precision");
        Check(Join(ids, "safe") == 9007199254740993UL && Join(ids, "max") == ulong.MaxValue, "split ids round trip exactly");
        Check(ids["povNeutral"] == uint.MaxValue && ids["signedMin"] == int.MinValue, "raw sentinel/sign round trip");
        var injected = SessionReader.Read(Path.Combine(root, "injected.jsonl")).Single(r => r.Sample != null).Sample.Channels;
        Check(Join(injected, "sample.0.batch") == 999999999999999UL && Join(injected, "sample.1.batch") == 999999999999999UL,
            "actual two-device core batch identifiers round trip exactly");
        Check(injected["sample.0.axes"] == 1 && injected["sample.1.buttonMask0"] == (1 << 11), "per-object injection marks survive");
        var dropped = SessionReader.Read(Path.Combine(root, "drop-then-stop.jsonl")).ToArray();
        Check(dropped.Last().Footer.Completed && dropped.Last().Footer.Counts.DroppedSamples == 0,
            "counterexample: writer/reader alone cannot know an upstream queue dropped a frame");
        var samples = dropped.Where(r => r.Sample != null).Select(r => r.Sample).ToArray();
        Check(Join(samples[1].Channels, "frame") != Join(samples[0].Channels, "frame") + 1,
            "domain continuity check detects the hidden producer drop");
        foreach (var file in new[] { "drop-abandon", "too-wide" }) {
            bool refused = false;
            try { SessionReader.Read(Path.Combine(root, file + ".jsonl")).ToArray(); }
            catch (SessionReadException e) { refused = e.Error == SessionReadError.MissingFooter; }
            Check(refused, "abandon/oversize cannot pass completion: " + file);
        }
        Console.WriteLine("PASS managed wire audit: complete frames, exact split IDs, upstream-drop false-completion counterexample, incomplete/oversize refusal");
    }
}
