$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Find-BuildTools.ps1')
$out = Join-Path $PSScriptRoot 'build\tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
foreach ($name in @('balance_policy_test','balance_runtime_test','balance_release_test')) {
    & $Compiler @CompileFlags /TC /std:c17 ('/Fe'+(Join-Path $out ($name+'.exe'))) ('/Fo'+(Join-Path $out ($name+'.obj'))) (Join-Path $PSScriptRoot ('client\'+$name+'.c')) /link @LinkFlags advapi32.lib
    if ($LASTEXITCODE -ne 0) { throw "Test build failed: $name" }
    & (Join-Path $out ($name+'.exe'))
    if ($LASTEXITCODE -ne 0) { throw "Test failed: $name" }
}
Add-Type -Path (Join-Path $PSScriptRoot 'support\VerifiedPowerSource.dll')
foreach($case in @(@(-1,0,$null),@(-1,2,'pd'),@(0,1,'barrel'),@(0,3,'barrel'),@(2,2,$null),@(2,0,$null),@(2,-1,$null))) {
    if([ChargingRibbonPolicy]::Next($case[0],$case[1]) -cne $case[2]) { throw 'Charging ribbon transition policy failed.' }
}
'PASS: charging ribbon transitions, battery removal, unchanged and unknown source'
