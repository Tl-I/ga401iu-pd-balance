$ErrorActionPreference = 'Stop'
Add-Type -Path (Join-Path $PSScriptRoot 'support\VerifiedPowerSource.dll')
$mutex = [Threading.Mutex]::new($false,'Local\Ga401ChargingIndicator')
$owned = $false
$wake = $null
$stop = $null
try {
    try { $owned = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $owned = $true }
    if (-not $owned) { return }
    $stop = [Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,'Local\Ga401ChargingIndicatorStop')
    $stop.Reset() | Out-Null
    # Subscribe before reading; an event between the read and wait is retained.
    $wake = [ChargerSourceEvent]::new()
    $last = -1
    while (-not $stop.WaitOne(0)) {
        $source = [VerifiedPowerSource]::Read()
        $valid = $source -ge 0 -and $source -le 3
        if ($valid -and $source -ne $last) {
            # Confirm only transitions; steady state has no periodic polling.
            for ($confirmation=0; $confirmation -lt 2; $confirmation++) {
                if ($stop.WaitOne(150)) { return }
                if ([VerifiedPowerSource]::Read() -ne $source) { $valid=$false; break }
            }
        }
        if ($valid) {
            $kind = [ChargingRibbonPolicy]::Next($last,$source)
            $last = $source
            if ($kind) {
                $info = [Diagnostics.ProcessStartInfo]::new()
                $info.FileName = Join-Path $PSScriptRoot 'PowerRibbon.exe'
                $info.UseShellExecute = $false
                $info.CreateNoWindow = $true
                $info.ArgumentList.Add($kind)
                $animation = [Diagnostics.Process]::Start($info)
                $animation.Dispose()
            }
        }
        $timeout = if ($valid) { [Threading.Timeout]::Infinite } else { 2000 }
        if ($wake.WaitOrStop($stop,$timeout)) { break }
    }
} finally {
    if ($wake) { $wake.Dispose() }
    if ($stop) { $stop.Dispose() }
    if ($owned) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
