using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;

// Only a fake child for the process supervisor. No devices, game, window or input implementation.
if (args.Length != 3 || args[0] != "--read-only-device-probe") return 9;
string mode = File.ReadAllText(args[1]).Trim();
Console.WriteLine("fixture child started: " + mode);
Console.Error.WriteLine("fixture stderr retained");
if (mode == "hang") { Thread.Sleep(60000); return 8; }
if (mode == "exit") return 7;
using var process = Process.GetCurrentProcess();
if (!Native.GetProcessTimes(process.Handle, out var created, out _, out _, out _)) return 10;
Directory.CreateDirectory(args[2]);
File.WriteAllText(Path.Combine(args[2], "host-shutdown.json"), JsonSerializer.Serialize(new {
    pid = process.Id + (mode == "pid" ? 1 : 0), creationTime = created + (mode == "creation" ? 1UL : 0UL),
    state = mode == "unresolved" ? "unresolved" : "resolved", unresolved = mode == "unresolved" ? new[] { "fixture" } : Array.Empty<string>(),
    inputReleased = mode != "input", released = mode == "held" ? 1 : mode == "three" ? 3 : 2, held = mode == "held" ? 1 : 0
}));
return 0;
static class Native {
    [DllImport("kernel32.dll", SetLastError=true)] [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool GetProcessTimes(IntPtr p, out ulong created, out ulong exited, out ulong kernel, out ulong user);
}
