# Generates tools/mission_routes.tsv from spec-mission-packages.md ("Per-mission objective routes", spec sync 40feafc).
# Columns: mission <TAB> step <TAB> kind <TAB> names (| separated; "" for 'any vehicle'/unnamed) <TAB> detail
# Rows with kind "hazard" list triggers the spec says the runner must never enter. Run from repo root.
$lines = Get-Content spec-mission-packages.md
$start = ($lines | Select-String -SimpleMatch '## Per-mission objective routes').LineNumber
$end = ($lines | Select-String -SimpleMatch '### Route target positions').LineNumber
$out = New-Object System.Collections.Generic.List[string]
$m = $null
for ($i = $start; $i -lt $end - 1; $i++) {
  $l = $lines[$i]
  if ($l -match '^#### (\S+) \(') { $m = $Matches[1]; continue }
  if (-not $m) { continue }
  if ($l -match '^\| (\d+) \| ([a-z-]+) \| `([^|]*)` \| ([^|]*) \|') {
    $step = $Matches[1]; $kind = $Matches[2]; $name = $Matches[3]; $detail = $Matches[4].Trim()
    if ($name -match '^any vehicle') { $name = '' }
    $name = $name -replace '^\{|\}(\[\*\])?$', ''
    $name = ($name -split ',\s*' | ForEach-Object { $_.Trim() }) -join '|'
    $out.Add("$m`t$step`t$kind`t$name`t$detail")
  } elseif ($l -match '^Hazards') {
    foreach ($h in [regex]::Matches($l, '`([^`]+)` \(')) { $out.Add("$m`t0`thazard`t$($h.Groups[1].Value)`t") }
  }
}
Set-Content -Path tools/mission_routes.tsv -Value $out -Encoding ascii
"$($out.Count) rows"
