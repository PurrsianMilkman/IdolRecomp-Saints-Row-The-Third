param([string]$Out, [string]$Exe, [string[]]$ArgList)
$p = Start-Process -FilePath $Exe -ArgumentList $ArgList -RedirectStandardOutput $Out -RedirectStandardError ($Out + ".err") -PassThru -NoNewWindow
$peak = 0
while (-not $p.HasExited) {
    try { $p.Refresh(); if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 } } catch {}
    Start-Sleep -Milliseconds 500
}
$p.WaitForExit()
"exit=$($p.ExitCode) peakWS_MB=$([math]::Round($peak/1MB))" | Tee-Object -FilePath ($Out + ".peak.txt")
