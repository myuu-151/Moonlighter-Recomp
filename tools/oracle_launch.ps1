# Runs the game for an oracle recording on a hidden Windows desktop, so its window never shows on the
# user's screen: nothing pops up, nothing flashes in the taskbar, nothing appears in a remote stream.
# (A desktop made with CreateDesktop in the same window station: the process runs and renders
# normally, it just isn't on the desktop the user sees. The oracle's Unobtrusive.cs still keeps the
# window small and minimized, which matters only with -Visible.)
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\oracle_launch.ps1 `
#       -Exe C:\...\Moonlighter\Moonlighter.exe -Arguments '-oracle-walk=roll -oracle-quit'
#
# Exits with the game's exit code; 124 after -TimeoutSec (the game is killed). -Visible runs it on
# the user's desktop instead (minimized), for debugging.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$Arguments = '',
    [string]$WorkingDirectory = '',
    [ValidateSet('Idle', 'BelowNormal', 'Normal')][string]$Priority = 'Normal',
    [int]$TimeoutSec = 1800,
    [string]$DesktopName = 'MoonlighterOracle',
    [switch]$Visible
)
$ErrorActionPreference = 'Stop'
if (-not $WorkingDirectory) { $WorkingDirectory = Split-Path -Parent $Exe }
# Windowed and small either way (a fullscreen switch on a hidden desktop has nothing to switch).
$fullArgs = "-screen-fullscreen 0 -screen-width 640 -screen-height 360 $Arguments"

if ($Visible) {
    $p = Start-Process $Exe -ArgumentList $fullArgs -WorkingDirectory $WorkingDirectory -WindowStyle Minimized -PassThru
    $null = $p.Handle
    try { $p.PriorityClass = $Priority } catch {}
} else {
    if (-not ('OracleHiddenDesktop' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

public static class OracleHiddenDesktop {
    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateDesktopW(string name, IntPtr device, IntPtr devmode, uint flags, uint access, IntPtr sa);
    [DllImport("user32.dll", SetLastError = true)]
    static extern bool CloseDesktop(IntPtr desktop);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO {
        public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags,
                                      IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    const uint GENERIC_ALL = 0x10000000;
    static IntPtr desktop = IntPtr.Zero;

    // The process id; the desktop stays open until Close (it lives while any thread uses it).
    public static int Start(string desktopName, string exe, string args, string cwd, uint priorityFlag) {
        desktop = CreateDesktopW(desktopName, IntPtr.Zero, IntPtr.Zero, 0, GENERIC_ALL, IntPtr.Zero);
        if (desktop == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateDesktop");
        var si = new STARTUPINFO();
        si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
        si.lpDesktop = "WinSta0\\" + desktopName;
        PROCESS_INFORMATION pi;
        var cmd = new StringBuilder("\"" + exe + "\" " + args);
        if (!CreateProcessW(exe, cmd, IntPtr.Zero, IntPtr.Zero, false, priorityFlag, IntPtr.Zero, cwd, ref si, out pi))
            throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateProcess");
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return pi.dwProcessId;
    }

    public static void Close() {
        if (desktop != IntPtr.Zero) { CloseDesktop(desktop); desktop = IntPtr.Zero; }
    }
}
'@
    }
    $flag = @{ Idle = 0x40; BelowNormal = 0x4000; Normal = 0x20 }[$Priority]
    $id = [OracleHiddenDesktop]::Start($DesktopName, $Exe, $fullArgs, $WorkingDirectory, [uint32]$flag)
    $p = Get-Process -Id $id
    $null = $p.Handle
}

$code = 0
try {
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        Write-Warning "oracle_launch: no exit after $TimeoutSec s; killing the game"
        try { $p.Kill(); $p.WaitForExit(10000) | Out-Null } catch {}
        $code = 124
    } else {
        $code = $p.ExitCode
    }
} finally {
    if (-not $Visible) { [OracleHiddenDesktop]::Close() }
}
exit $code
