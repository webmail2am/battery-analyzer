# Copyright (c) 2026 Ashoka Munasinghe, www.electronics.lk. MIT License, see LICENSE.
# SPDX-License-Identifier: MIT
# Regenerates the README screenshots of the PC app (docs/images/*.png) with headless
# Microsoft Edge, using the app's built-in demo. Run from PowerShell:
#   powershell -ExecutionPolicy Bypass -File docs\screenshots.ps1
$edge = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
$root = Split-Path $PSScriptRoot -Parent
$app  = "file:///" + ((Join-Path $root "tools\BatteryAnalyzer.html") -replace "\\", "/")

function Shot($name, $size, $hash) {
  $prof = Join-Path $env:TEMP ("edge-shot-" + [guid]::NewGuid())   # fresh profile: no old results
  $out = Join-Path $root "docs\images\$name"
  $args = @("--headless=new", "--disable-gpu", "--hide-scrollbars", "--no-first-run",
            "--user-data-dir=$prof", "--force-device-scale-factor=1", "--window-size=$size",
            "--virtual-time-budget=12000", "--screenshot=$out", "`"$app#$hash`"")
  Start-Process -FilePath $edge -ArgumentList $args -Wait -WindowStyle Hidden
  Remove-Item -Recurse -Force $prof -ErrorAction SilentlyContinue
  "{0}: {1} bytes" -f $name, (Get-Item $out).Length
}

Shot "app-full-test.png"       "1400,1215" "demo&run=1&mode=1&speed=1500&hold=1"
Shot "app-discharge-curve.png" "1400,640"  "demo&run=1&mode=1&speed=1500&hold=1&tab=cap"
Shot "app-quick-test.png"      "1400,1215" "demo&run=1&mode=0&speed=300&hold=1"
