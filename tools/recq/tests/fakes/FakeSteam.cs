// A stand-in for steam.exe in the recording queue's tests (tools/recq/tests/run_tests.ps1).
// Compiled into <scratch>/steam/recqtest_steam.exe (a Windows app: no console window).
//
//   recqtest_steam.exe -silent -log <connection_log.txt> [-delay <seconds>] [-never]
//
// Appends lines in the format of Steam's logs/connection_log.txt: Logged Off and Connecting at
// once, then after `delay` seconds Connected, Logging On and Logged On (with -never it keeps
// "Connecting" instead). Then it stays up until killed, like Steam.
using System;
using System.IO;
using System.Threading;

static class FakeSteam
{
    static string log;

    static void Line(string state, string text)
    {
        string l = "[" + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "] " +
                   (state == null ? "" : "[" + state + "] [U:1:1] ") + text + "\r\n";
        for (int i = 0; i < 50; i++)
        {
            try { File.AppendAllText(log, l); return; } catch (IOException) { Thread.Sleep(20); }
        }
    }

    static int Main(string[] args)
    {
        double delay = 1;
        bool never = false;
        for (int i = 0; i < args.Length; i++)
        {
            if (args[i] == "-log" && i + 1 < args.Length) log = args[++i];
            else if (args[i] == "-delay" && i + 1 < args.Length) delay = double.Parse(args[++i], System.Globalization.CultureInfo.InvariantCulture);
            else if (args[i] == "-never") never = true;
        }
        if (log == null) return 2;
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(log)));
        Line("Logged Off, 0, 0", "CCMInterface::SetSteamID( [U:1:1] )");
        Line("Connecting, 4, 0", "PingWebSocketCM() starting...");
        if (!never)
        {
            Thread.Sleep((int)(delay * 1000));
            Line("Connecting, 4, 7", "ConnectionCompleted() (fake)");
            Line("Connected, 4, 7", "Logging on [U:1:1]");
            Line("Logging On, 4, 7", "RecvMsgClientLogOnResponse() : [U:1:1] 'OK'");
            Line("Logged On, 4, 7", "RecvMsgClientLogOnResponse() : processing complete");
        }
        while (true)
        {
            Thread.Sleep(5000);
            if (never) Line("Connecting, 4, 0", "PingWebSocketCM() starting...");
        }
    }
}
