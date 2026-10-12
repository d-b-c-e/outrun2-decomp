using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Dbce.Wheel.Playback;

static partial class Program
{
    static int ComparisonCommand(string reference, string replay, string output, bool sealedInputs)
    {
        reference = Path.GetFullPath(reference); replay = Path.GetFullPath(replay); output = Path.GetFullPath(output);
        var held = new List<FileStream>();
        try
        {
            // No output in either input directory, and never replace an existing comparison or source file.
            foreach (var input in new[] { reference, replay })
            {
                var root = (sealedInputs ? input : Path.GetDirectoryName(input)).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
                Check(!output.StartsWith(root, StringComparison.OrdinalIgnoreCase), "comparison output outside captures required");
            }
            Check(!File.Exists(output), "new comparison output required");
            for (var p = Path.GetDirectoryName(output); p != null; p = Path.GetDirectoryName(p))
                Check((File.GetAttributes(p) & FileAttributes.ReparsePoint) == 0, "comparison output reparse path");
            string aSeal = null, bSeal = null;
            if (sealedInputs)
            {
                foreach (var dir in new[] { reference, replay })
                {
                    held.AddRange(FreezeMembers(dir, RecordingRequired.Concat(new[] { RecordingSeal })));
                    VerifyRecordingSeal(dir); ValidateQueued(dir);
                    using var report = JsonDocument.Parse(File.ReadAllText(Path.Combine(dir, RecordingReport)));
                    var r = report.RootElement;
                    ValidateRecordingReport(dir, Str(r, "requestId"), Str(r, "exeSha256"), Num(r, "processId"), Num(r, "processCreationFileTime"));
                }
                // Each report is internally bound; the two captures must also be from the same executable.
                using var a = JsonDocument.Parse(File.ReadAllText(Path.Combine(reference, RecordingReport)));
                using var b = JsonDocument.Parse(File.ReadAllText(Path.Combine(replay, RecordingReport)));
                Check(Str(a.RootElement, "exeSha256") == Str(b.RootElement, "exeSha256"), "same replay executable required");
                Check(!a.RootElement.TryGetProperty("action", out var referenceAction) || referenceAction.GetString() == "record",
                    "reference must be an ordinary recording");
                Check(Str(b.RootElement, "action") == "playback" && Str(b.RootElement, "stoppedBy") == "tape-end",
                    "replay capture reached tape end required");
                aSeal = ArtifactSeal.Hash(Path.Combine(reference, RecordingSeal));
                bSeal = ArtifactSeal.Hash(Path.Combine(replay, RecordingSeal));
                Check(Str(b.RootElement, "sourceSealSha256") == aSeal, "replay is bound to this reference seal");
                reference = Path.Combine(reference, "source.jsonl"); replay = Path.Combine(replay, "source.jsonl");
            }
            var result = ConsumedComparison.Compare(reference, replay);
            result.Sealed = sealedInputs; result.ReferenceSealSha256 = aSeal; result.ReplaySealSha256 = bSeal;
            if (sealedInputs && !result.Pose.Compared)
            { result.Problems.Add("product playback requires recorded active-car pose"); result.Valid = result.Followed = false; }
            ConsumedComparison.Write(result, output);
            var code = ConsumedComparison.ExitCode(result);
            Console.WriteLine($"{(code == 0 ? "PASS" : code == 2 ? "DIVERGED" : "INVALID")} consumed comparison: {result.PlayedRows}/{result.ReferenceRows} rows; {result.Pose.PairedRows} paired pose rows; {(sealedInputs ? "both recording seals verified" : "analysis only; unsealed sources, not product qualification")}");
            return code;
        }
        finally { foreach (var file in held) file.Dispose(); }
    }
}
