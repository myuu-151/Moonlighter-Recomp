// A stand-in for Moonlighter.exe in the recording queue's tests (tools/recq/tests/run_tests.ps1).
// Compiled by the tests into <scratch>/game/recqtest_game.exe (a Windows app: no console window).
//
// Like the oracle it writes into <exe dir>/../data/oracle and into <exe dir>/BepInEx/LogOutput.log;
// into the folder of -oracle-out=<dir> instead when its "plugin" (BepInEx/plugins/MoonlighterOracle.dll)
// contains that string in UTF-16, as a real oracle build that knows the option does.
// What it does comes from <exe dir>/fakegame.plan, one line per run (the first line is used and
// removed; no line means "ok"):
//
//   <behaviour> [sleep=<ms>] [files=a.jsonl,b.jsonl] [marker=NAME] [code=<exit code>] [steam=<process name>]
//
//   ok        sleep, write the files (content: the plugin's SHA-256 and the command line), the marker, exit
//   linger    as ok, then never quit (the runner kills it after its done-grace)
//   hang      never write anything, never quit (the timeout kills it)
//   early     exit after `sleep` (default 500 ms) with nothing written (a Steam / startup failure)
//   nothing   exit after `sleep` with nothing written
//   crash     write half of the first file, log an error, exit with `code` (default -1073740791)
//   nomarker  write the files, no marker
//   markeronly  the marker only
//   empty     zero-length files and the marker
//   failed    the oracle's FAILED marker
//   extra     as ok, plus other.jsonl (a file the request didn't expect) and a rewritten keep.jsonl
//   append    as ok, plus a line appended to trace.jsonl (the oracle's always-on log)
//   killsteam kill the processes named `steam`, then exit early (Steam closing during a queue)
//
// Files default to the mode in the command line: -oracle-enemy=X gives enemy_X.jsonl and ENEMY_X_DONE.
// Every run is appended to <exe dir>/fakegame.runs.log.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

static class FakeGame
{
    static string Sha(string path)
    {
        if (!File.Exists(path)) return "absent";
        using (var s = SHA256.Create())
        using (var f = File.OpenRead(path))
            return BitConverter.ToString(s.ComputeHash(f)).Replace("-", "").ToLowerInvariant();
    }

    static bool KnowsOracleOut(string plugin)
    {
        if (!File.Exists(plugin)) return false;
        byte[] b = File.ReadAllBytes(plugin);
        for (int off = 0; off < 2 && off < b.Length; off++)
            if (Encoding.Unicode.GetString(b, off, b.Length - off).Contains("-oracle-out=")) return true;
        return false;
    }

    static int Main(string[] args)
    {
        string exeDir = AppDomain.CurrentDomain.BaseDirectory;
        string plugin = Path.Combine(exeDir, "BepInEx", "plugins", "MoonlighterOracle.dll");
        string outDir = Path.GetFullPath(Path.Combine(exeDir, "..", "data", "oracle"));
        if (KnowsOracleOut(plugin))
            foreach (var a in args)
                if (a.StartsWith("-oracle-out=")) outDir = Path.GetFullPath(a.Substring("-oracle-out=".Length));
        Directory.CreateDirectory(outDir);
        string planPath = Path.Combine(exeDir, "fakegame.plan");
        string line = "ok";
        if (File.Exists(planPath))
        {
            var lines = File.ReadAllLines(planPath).Where(l => l.Trim() != "").ToList();
            if (lines.Count > 0)
            {
                line = lines[0].Trim();
                File.WriteAllLines(planPath, lines.Skip(1).ToArray());
            }
        }
        File.AppendAllText(Path.Combine(exeDir, "fakegame.runs.log"),
            DateTime.Now.ToString("o") + " | pid " + Process.GetCurrentProcess().Id + " | " + line + " | " + Environment.CommandLine + "\r\n");

        string[] tok = line.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
        string behaviour = tok[0];
        var opt = new Dictionary<string, string>();
        foreach (var t in tok.Skip(1))
        {
            int eq = t.IndexOf('=');
            if (eq > 0) opt[t.Substring(0, eq)] = t.Substring(eq + 1);
        }

        string enemy = null;
        foreach (var a in args)
            if (a.StartsWith("-oracle-enemy=")) { enemy = a.Substring(14); int c = enemy.IndexOf(':'); if (c >= 0) enemy = enemy.Substring(0, c); }
        string[] files = opt.ContainsKey("files") ? opt["files"].Split(',') : new[] { "enemy_" + (enemy ?? "Fake") + ".jsonl" };
        string marker = opt.ContainsKey("marker") ? opt["marker"] : "ENEMY_" + (enemy ?? "Fake") + "_DONE";
        int sleep = opt.ContainsKey("sleep") ? int.Parse(opt["sleep"]) : (behaviour == "early" || behaviour == "killsteam" ? 500 : 1500);
        int code = opt.ContainsKey("code") ? int.Parse(opt["code"]) : (behaviour == "crash" ? -1073740791 : 0);

        string bepinex = Path.Combine(exeDir, "BepInEx");
        Directory.CreateDirectory(bepinex);
        string pluginSha = Sha(plugin);
        var log = new StringBuilder();
        log.AppendLine("[Info   :   BepInEx] fake BepInEx log");
        log.AppendLine("[Info   :Moonlighter Oracle] fake run: " + line);
        log.AppendLine("[Info   :Moonlighter Oracle] plugin " + pluginSha);
        log.AppendLine("[Info   :Moonlighter Oracle] Oracle output: " + outDir);
        File.WriteAllText(Path.Combine(bepinex, "LogOutput.log"), log.ToString());

        string content = "{\"plugin\":\"" + pluginSha + "\",\"pid\":" + Process.GetCurrentProcess().Id + ",\"line\":\"" + line + "\"}\n";
        Action writeFiles = () => { foreach (var f in files) File.WriteAllText(Path.Combine(outDir, f), content); };
        Action writeMarker = () => File.WriteAllText(Path.Combine(outDir, marker), DateTime.Now.ToString("o") + " fake");

        switch (behaviour)
        {
            case "ok":
                Thread.Sleep(sleep); writeFiles(); writeMarker(); return code;
            case "linger":
                Thread.Sleep(sleep); writeFiles(); writeMarker(); Thread.Sleep(Timeout.Infinite); return 0;
            case "hang":
                Thread.Sleep(Timeout.Infinite); return 0;
            case "early":
            case "nothing":
                Thread.Sleep(sleep); return code;
            case "crash":
                Thread.Sleep(sleep);
                File.WriteAllText(Path.Combine(outDir, files[0]), content.Substring(0, content.Length / 2));
                File.AppendAllText(Path.Combine(bepinex, "LogOutput.log"), "[Error  :Moonlighter Oracle] fake crash\r\n");
                return code;
            case "nomarker":
                Thread.Sleep(sleep); writeFiles(); return code;
            case "markeronly":
                Thread.Sleep(sleep); writeMarker(); return code;
            case "empty":
                Thread.Sleep(sleep); foreach (var f in files) File.WriteAllText(Path.Combine(outDir, f), ""); writeMarker(); return code;
            case "failed":
                Thread.Sleep(sleep); File.WriteAllText(Path.Combine(outDir, "FAILED"), "fake failure"); return code;
            case "append":
                Thread.Sleep(sleep); writeFiles();
                File.AppendAllText(Path.Combine(outDir, "trace.jsonl"), "{\"appended by\":" + Process.GetCurrentProcess().Id + "}\n");
                writeMarker(); return code;
            case "extra":
                Thread.Sleep(sleep); writeFiles();
                File.WriteAllText(Path.Combine(outDir, "other.jsonl"), content);
                File.WriteAllText(Path.Combine(outDir, "keep.jsonl"), "rewritten by the run\n");
                writeMarker(); return code;
            case "killsteam":
                foreach (var p in Process.GetProcessesByName(opt.ContainsKey("steam") ? opt["steam"] : "recqtest_steam"))
                    try { p.Kill(); p.WaitForExit(5000); } catch { }
                Thread.Sleep(sleep); return code;
        }
        File.AppendAllText(Path.Combine(bepinex, "LogOutput.log"), "[Error  :Moonlighter Oracle] unknown behaviour " + behaviour + "\r\n");
        return 2;
    }
}
