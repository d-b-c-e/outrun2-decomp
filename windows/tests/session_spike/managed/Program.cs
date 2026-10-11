using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json.Nodes;
using Dbce.Wheel.Recording;

static class Program {
    static void Check(bool value, string what) { if (!value) throw new Exception(what); }
    static uint Limb(double value) {
        if (!double.IsFinite(value) || value != Math.Floor(value) || value < 0 || value > uint.MaxValue)
            throw new ArgumentException("Not an unsigned integer limb.");
        return checked((uint)value);
    }
    static ulong Join(System.Collections.Generic.IDictionary<string, double> c, string key) =>
        Limb(c[key + ".lo"]) | ((ulong)Limb(c[key + ".hi"]) << 32);
    static bool BudgetFits(long rowBytes, int hz, int seconds, long limit, long metadataBytes) {
        if (rowBytes <= 0 || hz <= 0 || seconds <= 0 || metadataBytes < 0) throw new ArgumentException("Invalid budget");
        return (decimal)rowBytes * (hz * (decimal)seconds + 1) + metadataBytes + 8192 <= limit;
    }
    static string DropVariant(string root, string name, bool marker = false, bool uncounted = false, bool custom = false) {
        var records = File.ReadAllLines(Path.Combine(root, "drop-then-stop.jsonl")).Select(l => JsonNode.Parse(l)).ToList();
        var footer = records.Last()["footer"]; var counts = footer["counts"];
        counts["droppedSamples"] = 1; counts["queueFullCount"] = uncounted ? 0 : 1;
        if (marker) {
            records.Insert(2, JsonNode.Parse("{\"kind\":\"marker\",\"marker\":{\"elapsedSeconds\":0.016,\"name\":\"inputGap\",\"detail\":\"one upstream frame lost\"}}"));
            counts["acceptedMarkers"] = 1; counts["writtenMarkers"] = 1;
        }
        if (custom) { footer["completed"] = false; footer["stopReason"] = "upstreamDrop"; }
        var content = string.Concat(records.Take(records.Count - 1).Select(r => r.ToJsonString() + "\n"));
        counts["dataBytesWritten"] = Encoding.UTF8.GetByteCount(content);
        string path = Path.Combine(root, name + ".jsonl");
        using var file = new FileStream(path, FileMode.CreateNew);
        var bytes = Encoding.UTF8.GetBytes(content + records.Last().ToJsonString() + "\n"); file.Write(bytes);
        return path;
    }
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
        foreach (var bad in new[] {1.5, -1, double.NaN, double.PositiveInfinity, (double)uint.MaxValue + 1}) {
            bool refused = false; try { Limb(bad); } catch (ArgumentException) { refused = true; }
            Check(refused, "invalid managed limb refused");
        }
        Check(ids["povNeutral"] == uint.MaxValue && ids["signedMin"] == int.MinValue, "raw sentinel/sign round trip");
        var injected = SessionReader.Read(Path.Combine(root, "injected.jsonl")).Single(r => r.Sample != null).Sample.Channels;
        Check(Join(injected, "sample.0.batch") == 999999999999999UL && Join(injected, "sample.1.batch") == 999999999999999UL,
            "actual two-device core batch identifiers round trip exactly");
        Check(injected["sample.0.axes"] == 1 && injected["sample.1.buttonMask0"] == (1 << 11), "per-object injection marks survive");
        var dropped = SessionReader.Read(Path.Combine(root, "drop-then-stop.jsonl")).ToArray();
        Check(dropped.Last().Footer.Completed && dropped.Last().Footer.Counts.DroppedSamples == 0,
            "counterexample: native API does not count upstream loss; completed only means stream closed normally");
        var samples = dropped.Where(r => r.Sample != null).Select(r => r.Sample).ToArray();
        Check(Join(samples[1].Channels, "frame") != Join(samples[0].Channels, "frame") + 1,
            "domain continuity check detects the hidden producer drop");
        foreach (bool marker in new[] {false, true}) {
            var variant = SessionReader.Read(DropVariant(root, marker ? "counted-with-marker" : "counted-drop", marker)).ToArray();
            var footer = variant.Last().Footer;
            Check(footer.Completed && footer.Counts.DroppedSamples == 1 && footer.Counts.QueueFullCount == 1,
                "v1 accepts normal completion with correctly counted loss");
            Check(footer.Counts.WrittenMarkers == (marker ? 1 : 0), "v1 already supports gap markers");
            Check(!(footer.Completed && footer.Counts.DroppedSamples == 0 && footer.Counts.DroppedMarkers == 0),
                "lossless qualification must refuse counted loss even when Completed");
        }
        foreach (bool custom in new[] {false, true}) {
            bool refused = false;
            try { SessionReader.Read(DropVariant(root, custom ? "custom-stop" : "inconsistent-drop", uncounted:!custom, custom:custom)).ToArray(); }
            catch (SessionReadException e) { refused = e.Error == SessionReadError.InvalidFooter; }
            Check(refused, "invalid counters/custom stop reason refused by existing v1 reader");
        }
        foreach (var file in new[] { "drop-abandon", "too-wide" }) {
            bool refused = false;
            try { SessionReader.Read(Path.Combine(root, file + ".jsonl")).ToArray(); }
            catch (SessionReadException e) { refused = e.Error == SessionReadError.MissingFooter; }
            Check(refused, "abandon/oversize cannot pass completion: " + file);
        }
        var lines = File.ReadAllLines(Path.Combine(root, "frames.jsonl"));
        var rowBytes = lines.Where(l => JsonNode.Parse(l)["kind"].GetValue<string>() == "sample")
            .Select(l => Encoding.UTF8.GetByteCount(l) + 1).ToArray();
        long headerBytes = Encoding.UTF8.GetByteCount(lines[0]) + 1;
        long limit = 64L * 1024 * 1024; int rate = 60;
        // These estimates use observed row maxima, not a production worst-case JSON size guarantee.
        double observedSeconds = (limit - headerBytes - 8192) / (rowBytes.Max() * (double)rate);
        Check(!BudgetFits(rowBytes.Max(), rate, 600, limit, headerBytes), "default 64 MiB cannot promise this full-fidelity 600s capture");
        Check(BudgetFits(rowBytes.Max(), rate, 600, 1024L * 1024 * 1024, headerBytes), "explicit larger writer budget fits measured two-device estimate");
        var budget = new { observedEstimateOnly = true, deviceCount = 2, channels = 360, hz = rate,
            meanRowBytes = rowBytes.Average(), maximumObservedRowBytes = rowBytes.Max(),
            defaultFileBytes = limit, estimatedSecondsAtMaximumObservedRow = observedSeconds,
            requested600sFitsDefault = false, requireProductionWorstCasePreflight = true };
        File.WriteAllText(Path.Combine(root, "budget.json"), System.Text.Json.JsonSerializer.Serialize(budget));
        Console.WriteLine($"Budget: max observed row {rowBytes.Max()} bytes at hypothetical60Hz; default64MiB estimated {observedSeconds:F1}s, 600s refused");
        Console.WriteLine("PASS managed wire audit: exact IDs; native uncounted-drop counterexample; existing-v1 counted drops/markers; lossless gate; invalid footer/limb and incomplete/oversize refusal");
    }
}
