// Managed port of shim_first_loop/compare_consumed.py at 86f4640. Keep the distinction between
// evidence validity, equal supplied input, and the game's observed following. No device/game calls.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;

internal static class ConsumedComparison
{
    internal static readonly string[] Game = { "gameInput.mode", "gameInput.gear", "gameInput.speed",
        "gameInput.consumedSteer", "gameInput.consumedAccel", "gameInput.consumedBrake" };
    internal const string Valid = "gameInput.poseValid";
    internal static readonly string[] Pose = new[] { "gameInput.posX", "gameInput.posY", "gameInput.posZ",
        "gameInput.vec20X", "gameInput.vec20Y", "gameInput.vec20Z" }
        .Concat(Enumerable.Range(0, 3).SelectMany(r => Enumerable.Range(0, 3).Select(c => $"gameInput.rot{r}{c}")))
        .Concat(new[] { "gameInput.carModel", "gameInput.stageKey", "gameInput.courseGate" }).ToArray();
    static readonly string[] ZeroCounts = { "droppedSamples", "droppedMarkers", "queueFullCount", "contentionCount",
        "invalidCount", "limitCount", "errorCount", "rejectedAfterStop" };
    static readonly JsonSerializerOptions Json = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase, WriteIndented = true };

    internal sealed class Difference
    {
        public int Row { get; set; }
        public string Channel { get; set; }
        public double Reference { get; set; }
        public double Replay { get; set; }
        public double? ReferenceMode { get; set; }
        public double? ReplayMode { get; set; }
    }
    internal sealed class Motion
    {
        public int ValidRows { get; set; }
        public double PathLength { get; set; }
        public double[][] Range { get; set; }
    }
    internal sealed class PoseResult
    {
        public bool Compared { get; set; }
        public string Scope { get; set; }
        public Motion Reference { get; set; }
        public Motion Replay { get; set; }
        public int PairedRows { get; set; }
    }
    internal sealed class Result
    {
        public string Schema { get; set; } = "outrun.consumed-comparison@1";
        public string ReferenceSealSha256 { get; set; }
        public string ReplaySealSha256 { get; set; }
        public bool Sealed { get; set; }
        public int ReferenceRows { get; set; }
        public int ReplayRows { get; set; }
        public int PlayedRows { get; set; }
        public int FedChannels { get; set; }
        public bool Valid { get; set; }
        public List<string> Problems { get; set; } = new();
        public PoseResult Pose { get; set; }
        public int FedEqualRows { get; set; }
        public Difference FedFirstDifference { get; set; }
        public bool Followed { get; set; }
        public Difference FirstDivergence { get; set; }
        public double?[][] ReferenceModes { get; set; }
        public double?[][] ReplayModes { get; set; }
        public int? ReferenceRaceRow { get; set; }
        public int? ReplayRaceRow { get; set; }
    }
    sealed class Row : Dictionary<string, double?> { }
    static double? Number(Row row, string key) => row.TryGetValue(key, out var v) ? v : null;
    static double? Number(JsonElement value) => value.ValueKind == JsonValueKind.Number &&
        value.TryGetDouble(out var v) && double.IsFinite(v) ? v : null;
    static double? Number(JsonElement value, string key) => value.ValueKind == JsonValueKind.Object &&
        value.TryGetProperty(key, out var v) ? Number(v) : null;
    static void Unique(JsonElement value)
    {
        if (value.ValueKind == JsonValueKind.Object)
        {
            var seen = new HashSet<string>(StringComparer.Ordinal);
            foreach (var field in value.EnumerateObject())
            {
                if (!seen.Add(field.Name)) throw new InvalidDataException("duplicate JSON field: " + field.Name);
                Unique(field.Value);
            }
        }
        else if (value.ValueKind == JsonValueKind.Array) foreach (var item in value.EnumerateArray()) Unique(item);
    }
    static List<Row> Read(string path, List<string> problems, string name)
    {
        var rows = new List<Row>(); JsonElement? footer = null;
        // Product mode also holds the existing finalizer's read locks and one-GiB file bound.
        // This bound applies to analysis-only inputs too; keep only the comparison channels in memory.
        if (new FileInfo(path).Length > 1024L * 1024 * 1024) throw new InvalidDataException("comparison source exceeds one GiB");
        using var input = new StreamReader(path);
        string line; int lineNumber = 0;
        while ((line = input.ReadLine()) != null)
        {
            ++lineNumber;
            if (footer != null) { problems.Add(name + ": a record after the footer"); break; }
            try
            {
                using var doc = JsonDocument.Parse(line, new JsonDocumentOptions { MaxDepth = 32 });
                var item = doc.RootElement; Unique(item);
                if (!item.TryGetProperty("kind", out var kind)) throw new InvalidDataException("missing record kind");
                if (kind.GetString() == "sample")
                {
                    if (rows.Count >= 100000) throw new InvalidDataException("comparison sample bound exceeded");
                    var channels = item.GetProperty("sample").GetProperty("channels");
                    if (channels.ValueKind != JsonValueKind.Object) throw new InvalidDataException("sample channels are not an object");
                    var row = new Row();
                    foreach (var c in channels.EnumerateObject())
                        if (c.Name.StartsWith("final.", StringComparison.Ordinal) || c.Name.StartsWith("gameInput.", StringComparison.Ordinal) || c.Name == "replay.tapeRow")
                            row.Add(c.Name, Number(c.Value));
                    rows.Add(row);
                }
                else if (kind.GetString() == "footer") footer = item.GetProperty("footer").Clone();
            }
            catch (Exception e) when (e is JsonException or InvalidDataException or InvalidOperationException or KeyNotFoundException)
            { problems.Add($"{name}: invalid record at line {lineNumber}: {e.Message}"); break; }
        }
        if (footer == null) problems.Add(name + ": no footer (the stream did not end)");
        else
        {
            var f = footer.Value;
            if (f.ValueKind != JsonValueKind.Object || !f.TryGetProperty("completed", out var completed) || completed.ValueKind != JsonValueKind.True)
                problems.Add(name + ": the footer is not completed");
            var counts = f.ValueKind == JsonValueKind.Object && f.TryGetProperty("counts", out var c) ? c : default;
            if (ZeroCounts.Any(k => Number(counts, k) != 0)) problems.Add(name + ": footer counts missing or not zero");
            if (Number(counts, "writtenSamples") != rows.Count) problems.Add(name + ": written sample count does not match rows");
            if (Number(counts, "acceptedSamples") != Number(counts, "writtenSamples")) problems.Add(name + ": accepted/written sample counts differ");
        }
        return rows;
    }
    static double?[][] Modes(List<Row> rows)
    {
        var modes = new List<double?[]>();
        for (int i = 0; i < rows.Count; ++i)
        {
            var mode = Number(rows[i], "gameInput.mode");
            if (modes.Count == 0 || modes[^1][1] != mode) modes.Add(new double?[] { i, mode });
        }
        return modes.ToArray();
    }
    static int? RaceRow(List<Row> rows)
    { for (int i = 0; i < rows.Count; ++i) if (Number(rows[i], "gameInput.mode") is 16 or 18) return i; return null; }
    static Motion Measure(List<Row> rows, List<string> problems, string name)
    {
        var active = rows.Where(c => Number(c, Valid) == 1).ToArray();
        if (active.Length == 0) { problems.Add($"the {name} has no row with a valid pose (the active player car in a race)"); return null; }
        var positions = active.Select(c => new[] { Number(c, "gameInput.posX"), Number(c, "gameInput.posY"), Number(c, "gameInput.posZ") }).ToArray();
        if (positions.Any(p => p.Any(v => v == null))) { problems.Add($"the {name}: a valid pose row has a missing or non-finite position"); return null; }
        double distance = 0;
        for (int i = 1; i < positions.Length; ++i)
        {
            var delta = Enumerable.Range(0, 3).Select(j => positions[i][j].Value - positions[i - 1][j].Value).ToArray();
            // Scale to avoid overflow when checking malformed but finite values.
            var scale = delta.Max(Math.Abs);
            distance += scale == 0 ? 0 : scale * Math.Sqrt(delta.Sum(v => (v / scale) * (v / scale)));
        }
        if (!double.IsFinite(distance)) { problems.Add($"the {name} motion is not finite"); return null; }
        if (distance <= 0) problems.Add($"the car never moved in the {name}'s valid pose rows");
        return new Motion { ValidRows = active.Length, PathLength = Math.Round(distance, 3),
            Range = Enumerable.Range(0, 3).Select(i => new[] { Math.Round(positions.Min(p => p[i].Value), 3), Math.Round(positions.Max(p => p[i].Value), 3) }).ToArray() };
    }
    static double Tolerance(string name) => name == "gameInput.speed" ? .01 : name is "gameInput.posX" or "gameInput.posY" or "gameInput.posZ" ? 1e-3 :
        name.StartsWith("gameInput.vec20", StringComparison.Ordinal) ? 1e-4 : name.StartsWith("gameInput.rot", StringComparison.Ordinal) ? 1e-5 : 0;
    internal static Motion RequirePlaybackSource(string path)
    {
        var problems = new List<string>(); var rows = Read(path, problems, "reference");
        if (rows.Count == 0) problems.Add("the reference has no rows");
        var required = Game.Concat(Pose).Concat(new[] { Valid, "final.pcDrive" }).ToArray();
        for (int i = 0; i < rows.Count; ++i)
        {
            if (required.Any(k => Number(rows[i], k) == null))
            { problems.Add($"reference row {i}: playback needs finite game, input and pose channels"); break; }
            if (rows[i][Valid] is not (0 or 1))
            { problems.Add($"reference row {i}: poseValid is not 0 or 1"); break; }
        }
        var motion = Measure(rows, problems, "reference");
        if (problems.Count > 0) throw new InvalidDataException("Recording cannot be played: " + string.Join("; ", problems.Take(4)));
        return motion;
    }
    internal static Result Compare(string referencePath, string replayPath)
    {
        var result = new Result(); var problems = result.Problems;
        var reference = Read(referencePath, problems, "reference"); var replay = Read(replayPath, problems, "replay");
        if (reference.Count == 0) problems.Add("the reference has no rows");
        var fed = reference.Count > 0 ? reference[0].Keys.Where(k => k.StartsWith("final.", StringComparison.Ordinal)).OrderBy(k => k, StringComparer.Ordinal).ToArray() : Array.Empty<string>();
        if (reference.Count > 0 && !fed.Contains("final.pcDrive")) problems.Add("the reference has no final.pcDrive (recorded before the replay seam)");
        var played = new List<Row>(); bool tail = false;
        for (int i = 0; i < replay.Count; ++i)
        {
            var r = Number(replay[i], "replay.tapeRow");
            if (r == null || r != Math.Floor(r.Value)) { problems.Add($"replay row {i}: tapeRow is not an exact integer"); break; }
            if (r == -1) { tail = true; continue; }
            if (tail) { problems.Add($"replay row {i}: tape row after the neutral tail began"); break; }
            if (r != played.Count) { problems.Add($"replay row {i}: tape row {r}, expected {played.Count} (in order, each once)"); break; }
            played.Add(replay[i]);
        }
        if (reference.Count > 0 && played.Count != reference.Count) problems.Add($"{played.Count} of {reference.Count} reference rows played");
        var poseNames = new[] { Valid }.Concat(Pose).ToArray();
        bool pose = reference.Any(c => poseNames.Any(c.ContainsKey));
        var always = Game.Concat(pose ? new[] { Valid } : Array.Empty<string>()).ToArray();
        result.Pose = new PoseResult { Compared = pose, Scope = pose ? "input, startup and pose" : "input and startup only (the reference has no pose channels)" };
        if (pose) { result.Pose.Reference = Measure(reference, problems, "reference"); result.Pose.Replay = Measure(played, problems, "replay"); }
        result.ReferenceRows = reference.Count; result.ReplayRows = replay.Count; result.PlayedRows = played.Count; result.FedChannels = fed.Length;
        result.ReferenceModes = Modes(reference); result.ReplayModes = Modes(played);
        result.ReferenceRaceRow = RaceRow(reference); result.ReplayRaceRow = RaceRow(played);
        for (int i = 0; i < Math.Min(reference.Count, played.Count); ++i)
        {
            var a = reference[i]; var b = played[i];
            var missing = always.Concat(pose ? Pose : Array.Empty<string>()).Concat(fed).Where(k => Number(a, k) == null || Number(b, k) == null).Take(4).ToArray();
            if (missing.Length > 0) { problems.Add($"row {i}: missing or non-finite {string.Join(", ", missing)}"); break; }
            if (pose && (a[Valid] is not (0 or 1) || b[Valid] is not (0 or 1))) { problems.Add($"row {i}: poseValid is not 0 or 1"); break; }
            if (fed.All(k => a[k] == b[k])) ++result.FedEqualRows;
            else if (result.FedFirstDifference == null)
            {
                var k = fed.First(k => a[k] != b[k]);
                result.FedFirstDifference = new Difference { Row = i, Channel = k, Reference = a[k].Value, Replay = b[k].Value };
            }
            bool paired = pose && a[Valid] == 1 && b[Valid] == 1;
            if (paired) ++result.Pose.PairedRows;
            if (result.FirstDivergence == null)
                foreach (var k in always.Concat(paired ? Pose : Array.Empty<string>()))
                    if (Math.Abs(a[k].Value - b[k].Value) > Tolerance(k))
                    {
                        result.FirstDivergence = new Difference { Row = i, Channel = k, Reference = a[k].Value, Replay = b[k].Value,
                            ReferenceMode = a["gameInput.mode"], ReplayMode = b["gameInput.mode"] };
                        break;
                    }
        }
        result.Valid = problems.Count == 0; result.Followed = result.Valid && result.FirstDivergence == null;
        return result;
    }
    internal static void Write(Result result, string path)
    { using var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write); JsonSerializer.Serialize(output, result, Json); }
    internal static int ExitCode(Result r) => !r.Valid ? 1 : r.Followed && r.FedEqualRows == r.ReferenceRows ? 0 : 2;
}
