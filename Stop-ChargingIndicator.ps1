$ErrorActionPreference = 'Stop'
try { $stop = [Threading.EventWaitHandle]::OpenExisting('Local\Ga401ChargingIndicatorStop') }
catch [Threading.WaitHandleCannotBeOpenedException] { 'Charging indicator is not running.'; return }
try { $stop.Set() | Out-Null; 'Charging indicator stop requested.' }
finally { $stop.Dispose() }
