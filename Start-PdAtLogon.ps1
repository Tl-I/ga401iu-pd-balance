$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$logDir = Join-Path $root 'logs'
New-Item -ItemType Directory -Force $logDir | Out-Null
$log = Join-Path $logDir ('startup-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
Start-Transcript -Path $log | Out-Null
try {
    $active = Get-CimInstance Win32_Process -Filter "Name='ga401signed.exe'" |
        Where-Object ExecutablePath -eq (Join-Path $root 'client\x64\Release\ga401signed.exe')
    if ($active) { Write-Output 'Controller already running; no duplicate start.'; exit 0 }
    while ($true) {
    try {
    $marker = Join-Path $root 'client\x64\Release\ga401signed.exe.pending'
    if (Test-Path -LiteralPath $marker) {
        # Recover only values owned by the durable journal, or verify that EC
        # already restored control. Native recovery deletes the marker only after
        # rereading actual charger current and matching the current EC request.
        & (Join-Path $root 'Test-SignedIo.ps1') -RecoverPending -WaitForIdle
        if (-not $?) { throw 'Previous session state could not be verified.' }
        if (Test-Path -LiteralPath $marker) {throw 'Recovery did not verify and clear the pending session.'}
        Write-Output 'Previous session restored and verified; resuming automatic control.'
    }
    & (Join-Path $root 'Test-SignedIo.ps1') -Automatic 2>&1 | Tee-Object -FilePath ($log + '.runtime')
    if($global:Ga401ControllerResult -eq 31){continue}
    break # A normal stop is intentional; do not undo the user's stop request.
    } catch {
        Write-Output ("Controller attempt failed: {0}; retry in 2 seconds (no attempt limit)." -f $_.Exception.Message)
        Start-Sleep -Seconds 2
    }
    }
} finally { Stop-Transcript | Out-Null }
