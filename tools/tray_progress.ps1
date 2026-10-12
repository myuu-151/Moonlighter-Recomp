# The port's progress in the Windows taskbar's notification area, next to the clock: an icon
# with the percentage ported and a bar (gold: ported, green: verified against the game), the
# full numbers on hover. It reads progress/progress.json (tools/progress.py writes it on every
# commit) every 10 seconds, so it follows the work. Left click: the README's progress table on
# GitHub; right click: refresh or quit.
#
#   powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File tools\tray_progress.ps1
#   (tools\tray_progress_startup.py installs it to start with Windows)
param([string]$Progress = (Join-Path $PSScriptRoot '..\progress\progress.json'))

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$tray = New-Object System.Windows.Forms.NotifyIcon
$tray.Visible = $true
$script:lastIcon = $null
$script:lastStamp = $null

function New-ProgressIcon([double]$ported, [double]$verified) {
    $size = 32
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.Clear([System.Drawing.Color]::Transparent)
    # The number: whole percent, or one decimal under 10.
    $text = if ($ported -lt 10) { '{0:0.0}' -f $ported } else { '{0:0}' -f $ported }
    $font = New-Object System.Drawing.Font 'Segoe UI', 13, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = 'Center'
    $g.DrawString($text, $font, [System.Drawing.Brushes]::White, (New-Object System.Drawing.RectangleF 0, 1, $size, 18), $fmt)
    # The bar: an outline, the ported share in gold, the verified share in green over it.
    $bar = New-Object System.Drawing.Rectangle 2, 21, ($size - 5), 8
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(70, 70, 70))), $bar)
    $pw = [math]::Max(1, [math]::Round(($size - 5) * $ported / 100))
    $vw = [math]::Round(($size - 5) * $verified / 100)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(232, 180, 60))), 2, 21, $pw, 8)
    if ($vw -gt 0) { $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(90, 200, 110))), 2, 21, $vw, 8) }
    $g.DrawRectangle((New-Object System.Drawing.Pen ([System.Drawing.Color]::White), 1), $bar)
    $g.Dispose()
    $icon = [System.Drawing.Icon]::FromHandle($bmp.GetHicon())
    return $icon
}

function Update-Tray {
    try {
        $item = Get-Item $Progress -ErrorAction Stop
        if ($script:lastStamp -eq $item.LastWriteTimeUtc) { return }
        $script:lastStamp = $item.LastWriteTimeUtc
        $p = Get-Content $Progress -Raw | ConvertFrom-Json
        $old = $script:lastIcon
        $script:lastIcon = New-ProgressIcon $p.percent $p.verifiedPercent
        $tray.Icon = $script:lastIcon
        if ($old) { $old.Dispose() }
        # (a tooltip holds 63 characters)
        $tray.Text = ('Moonlighter port: {0:0.0}% ported, {1:0.0}% verified' -f $p.percent, $p.verifiedPercent)
    } catch {
        $tray.Text = 'Moonlighter port: progress.json not found'
    }
}

$menu = New-Object System.Windows.Forms.ContextMenuStrip
[void]$menu.Items.Add('Refresh', $null, { $script:lastStamp = $null; Update-Tray })
[void]$menu.Items.Add('Quit', $null, { $tray.Visible = $false; [System.Windows.Forms.Application]::Exit() })
$tray.ContextMenuStrip = $menu
$tray.add_MouseClick({
    param($sender, $e)
    if ($e.Button -eq 'Left') { Start-Process 'https://github.com/myuu-151/Moonlighter-Recomp#progress' }
})

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 10000
$timer.add_Tick({ Update-Tray })
$timer.Start()
Update-Tray
[System.Windows.Forms.Application]::Run()
$tray.Dispose()
