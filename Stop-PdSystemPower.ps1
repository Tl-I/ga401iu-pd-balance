$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $root 'client\x64\Release\ga401control.exe') --pd-balance-stop
if ($LASTEXITCODE -ne 0) { throw 'No active PD balance session was stopped.' }
Write-Output 'Stop requested; the running session restores charger control and unloads its driver.'
