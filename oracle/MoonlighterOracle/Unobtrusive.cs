using System;
using System.Collections;
using System.Diagnostics;
using System.Runtime.InteropServices;
using UnityEngine;

namespace MoonlighterOracle
{
    /// While an oracle mode runs (any `-oracle-*` argument), the game stays out of the way of
    /// whoever is using the PC: windowed and small even if its options ask for fullscreen, kept
    /// running in the background, and its window minimized without taking the focus as soon as
    /// it exists (and again if the game shows it). `-oracle-visible` leaves the window alone.
    /// Recordings don't depend on the window: they log the frame's own clocks.
    public static class Unobtrusive
    {
        [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
        [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr hWnd);
        [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hWnd);
        [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc proc, IntPtr param);
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr hWnd, System.Text.StringBuilder name, int max);
        private delegate bool EnumProc(IntPtr hWnd, IntPtr param);
        private const int SW_SHOWMINNOACTIVE = 7;

        // The game's own window (Unity's class; Process.MainWindowHandle can be BepInEx's console).
        private static IntPtr GameWindow()
        {
            uint me = (uint)Process.GetCurrentProcess().Id;
            IntPtr found = IntPtr.Zero;
            var name = new System.Text.StringBuilder(64);
            EnumWindows((h, p) =>
            {
                GetWindowThreadProcessId(h, out uint pid);
                if (pid != me) return true;
                name.Length = 0;
                GetClassName(h, name, name.Capacity);
                if (name.ToString() != "UnityWndClass" || !IsWindowVisible(h)) return true; // (the shown one)
                found = h;
                return false;
            }, IntPtr.Zero);
            return found;
        }

        public static bool Wanted(string[] args)
        {
            if (Array.IndexOf(args, "-oracle-visible") >= 0) return false;
            return Array.Exists(args, a => a.StartsWith("-oracle-"));
        }

        public static IEnumerator Keep()
        {
            Application.runInBackground = true;
            // Minimized there's no vsync to pace it: 60 frames a second, as when it's shown.
            Application.targetFrameRate = 60;
            while (true)
            {
                IntPtr window = GameWindow();
                bool minimized = window != IntPtr.Zero && IsIconic(window);
                // (Not while minimized: the screen's size then isn't the window's.)
                if (!minimized && (Screen.fullScreen || Screen.width > 640))
                    Screen.SetResolution(640, 360, FullScreenMode.Windowed);
                if (window != IntPtr.Zero && !minimized)
                {
                    ShowWindow(window, SW_SHOWMINNOACTIVE);
                    OraclePlugin.Log.LogInfo("unobtrusive: minimized the game's window");
                }
                yield return new WaitForSecondsRealtime(0.5f);
            }
        }
    }
}
