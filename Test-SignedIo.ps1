# Default cache-only; -Sample reads charger/battery without charger writes.
param([switch]$Sample, [switch]$RecoveryTest, [switch]$Automatic, [switch]$VerifyIdle, [switch]$WaitForIdle,
      [switch]$RecoverPending, [ValidatePattern('^[0-9A-Fa-f]{4}$')][string]$RecoverLegacy,
      [ValidateRange(0,3600)][int]$Seconds = 0)
$ErrorActionPreference = 'Stop'
$global:Ga401ControllerResult=0
. (Join-Path $PSScriptRoot 'DriverPath.ps1')
. (Join-Path $PSScriptRoot 'Invoke-HiddenNative.ps1')
$driver = Join-Path $PSScriptRoot 'third_party\inpout\inpoutx64.sys'
$client = Join-Path $PSScriptRoot 'client\x64\Release\ga401signed.exe'
$expected = 'F8965FDCE668692C3785AFA3559159F9A18287BC0D53ABB21902895A8ECF221B'
if ((Get-FileHash -LiteralPath $driver -Algorithm SHA256).Hash -ne $expected) { throw 'Unexpected signed driver hash.' }
if ((Get-AuthenticodeSignature -LiteralPath $driver).Status -ne 'Valid') { throw 'Driver signature verification failed.' }
$existing = Get-CimInstance Win32_SystemDriver -Filter "Name='Ga401SignedIoCheck'"
$reuseRunning = $false
if ($existing) {
    if ((ConvertTo-NormalDriverPath $existing.PathName) -ne (ConvertTo-NormalDriverPath $driver)) {
        throw 'Existing service has a different driver path.'
    }
    if($existing.State -eq 'Running'){
        if(Get-Process ga401signed -ErrorAction SilentlyContinue){throw 'Signed controller is already active.'}
        $reuseRunning=$true
    } elseif($existing.State -ne 'Stopped'){throw 'Driver service is transitioning; retry later.'}
    # A stopped, exact-path service can remain after Windows shutdown.
    if(-not $reuseRunning){
        Invoke-HiddenNative "$env:WINDIR\System32\sc.exe" @('delete','Ga401SignedIoCheck')
        if ($LASTEXITCODE -ne 0) { throw 'Could not remove stale stopped service.' }
    }
}
if ((Get-Service Ga401Ec -ErrorAction SilentlyContinue).Status -eq 'Running') { throw 'Old EC driver is running.' }
$created = $false
try {
    if(-not $reuseRunning){
    Invoke-HiddenNative "$env:WINDIR\System32\sc.exe" @('create','Ga401SignedIoCheck','type=','kernel','start=','demand','binPath=',$driver)
    if ($LASTEXITCODE -ne 0) { throw 'Could not create check service.' }
    $created = $true
    Invoke-HiddenNative "$env:WINDIR\System32\sc.exe" @('start','Ga401SignedIoCheck')
    if ($LASTEXITCODE -ne 0) { throw 'Windows rejected the driver. Do not weaken security policies to force it.' }
    } else {$created=$true;Write-Output 'Reusing verified signed driver left by an exited controller.'}
    if ($RecoverPending -or $RecoverLegacy) {
        Add-Type -Path (Join-Path $PSScriptRoot 'support\VerifiedPowerSource.dll')
        $sourceWake=[ChargerSourceEvent]::new()
        try {
        do {
            if($WaitForIdle){
                $chargerSource=[VerifiedPowerSource]::Read()
                while($chargerSource -eq 0 -or $chargerSource -eq 1){
                    Write-Output "Recovery suspended for non-PD source=$chargerSource; pending retained; waiting for power event, no periodic EC access."
                    $sourceWake.Wait()
                    $chargerSource=[VerifiedPowerSource]::Read()
                }
            }
            if($RecoverLegacy){Invoke-HiddenNative $client @('--recover-legacy',$RecoverLegacy)}
            else {Invoke-HiddenNative $client @('--recover-pending')}
            $recoveryExit=$LASTEXITCODE
            if($recoveryExit -eq 29 -and $WaitForIdle){
                Write-Output 'Recovery waiting: PD/transport not ready; pending record retained; fixed 2-second retry.'
                Start-Sleep -Seconds 2
            }
        } while($recoveryExit -eq 29 -and $WaitForIdle)
        $global:LASTEXITCODE=$recoveryExit
        } finally {$sourceWake.Dispose()}
    }
    elseif ($VerifyIdle) {
        do {
            Invoke-HiddenNative $client @('--verify-idle')
            $verifyExit=$LASTEXITCODE
            if($verifyExit -eq 29 -and $WaitForIdle){
                Write-Output 'Waiting for PD before read-only previous-session verification; no charger writes.'
                Start-Sleep -Seconds 30
            }
        } while($verifyExit -eq 29 -and $WaitForIdle)
        $global:LASTEXITCODE=$verifyExit
    }
    elseif ($Automatic) {
        Add-Type -Path (Join-Path $PSScriptRoot 'support\VerifiedPowerSource.dll')
        $sourceWake=[ChargerSourceEvent]::new()
        try {
            $chargerSource=[VerifiedPowerSource]::Read()
            while($chargerSource -eq 0 -or $chargerSource -eq 1){
                Write-Output "Automatic waiting for PD event; source=$chargerSource; no periodic EC access."
                $sourceWake.Wait();$chargerSource=[VerifiedPowerSource]::Read()
            }
            Invoke-HiddenNative $client @('--pd-auto',"$Seconds")
            $global:Ga401ControllerResult=$LASTEXITCODE
        } finally {$sourceWake.Dispose()}
    }
    elseif ($RecoveryTest) { Invoke-HiddenNative $client @('--recovery-test') }
    elseif ($Sample) { Invoke-HiddenNative $client @('--sample') } else { Invoke-HiddenNative $client @('--check') }
    if ($Automatic -and $global:Ga401ControllerResult -eq 31) {
        Write-Output 'PD disconnected; normal handoff to event-driven supervisor, not a communication fault.'
    } elseif ($LASTEXITCODE -ne 0) { throw 'Signed I/O cache check failed.' }
} finally {
    if ($created) {
        $service = Get-Service Ga401SignedIoCheck -ErrorAction SilentlyContinue
        if ($service.Status -ne 'Stopped') {
            Invoke-HiddenNative "$env:WINDIR\System32\sc.exe" @('stop','Ga401SignedIoCheck')
            $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(10))
        }
        Invoke-HiddenNative "$env:WINDIR\System32\sc.exe" @('delete','Ga401SignedIoCheck')
    }
}
