$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Run this installer from PowerShell 7.' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run as administrator.' }
$user = $identity.Name
$pwsh = (Get-Process -Id $PID).Path
$launcher = Join-Path $PSScriptRoot 'HiddenTask.exe'
$script = Join-Path $PSScriptRoot 'Start-PdAtLogon.ps1'
foreach ($required in @($launcher, (Join-Path $PSScriptRoot 'client\x64\Release\ga401signed.exe'), (Join-Path $PSScriptRoot 'support\VerifiedPowerSource.dll'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing build output: $required" }
}
$action = New-ScheduledTaskAction -Execute $launcher -Argument ('"{0}" "{1}"' -f $pwsh,$script) -WorkingDirectory $PSScriptRoot
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $user
$trigger.Delay = 'PT15S'
$runAs = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::Zero)
Register-ScheduledTask -TaskName 'GA401 PD Battery Balance' -Action $action -Trigger $trigger -Principal $runAs -Settings $settings -Description 'GA401IU PD battery-current feedback; supervised recovery and orderly stop.' -Force | Select-Object TaskName,State
$indicatorScript = Join-Path $PSScriptRoot 'Start-ChargingIndicator.ps1'
$indicatorAction = New-ScheduledTaskAction -Execute $launcher -Argument ('"{0}" "{1}"' -f $pwsh,$indicatorScript) -WorkingDirectory $PSScriptRoot
Register-ScheduledTask -TaskName 'GA401 Charging Indicator' -Action $indicatorAction -Trigger $trigger -Principal $runAs -Settings $settings -Description 'Three-second charging ribbon on verified charger transitions; event-based wait.' -Force | Select-Object TaskName,State
