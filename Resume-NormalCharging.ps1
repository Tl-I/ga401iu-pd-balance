$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $root 'client\x64\Release\ga401control.exe') --pd-recharge
if ($LASTEXITCODE -ne 0) { throw 'No active automatic controller.' }
Write-Output 'EC charging control requested. Automatic hold waits for the next threshold crossing; the charge limit is unchanged.'
