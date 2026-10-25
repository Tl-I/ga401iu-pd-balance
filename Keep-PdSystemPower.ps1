param([switch]$Foreground, [ValidateRange(0,3600)][int]$Seconds = 0)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (Test-Path -LiteralPath (Join-Path $root 'runtime-validation.pending')) {
    throw 'Live regulation is pending signed-backend and recovery validation. Run Test-SignedIo.ps1 for a cache-only check.'
}
if ($Foreground) {
    & (Join-Path $root 'Test-SignedIo.ps1') -Automatic -Seconds $Seconds
    exit $LASTEXITCODE
}
if ((Get-Service -Name Ga401Ec -ErrorAction SilentlyContinue).Status -eq 'Running' -or
    (Get-Service -Name Ga401SignedIoCheck -ErrorAction SilentlyContinue)) {
    throw 'Ga401Ec is already loaded. Stop the current session first.'
}
$logDir = Join-Path $root 'logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$run = Start-Process -FilePath (Get-Process -Id $PID).Path -WindowStyle Hidden `
    -ArgumentList @('-NoProfile', '-File', ('"{0}"' -f $PSCommandPath), '-Foreground', '-Seconds', $Seconds) `
    -RedirectStandardOutput (Join-Path $logDir "$stamp.log") `
    -RedirectStandardError (Join-Path $logDir "$stamp.error.log") -PassThru
Write-Output "PD balance starting: PID=$($run.Id), log=$logDir\$stamp.log"
