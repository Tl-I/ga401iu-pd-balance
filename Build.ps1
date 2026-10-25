$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Find-BuildTools.ps1')
$out = Join-Path $PSScriptRoot 'client\x64\Release'
New-Item -ItemType Directory -Force -Path $out | Out-Null
foreach ($name in @('ga401signed','ga401control')) {
    & $Compiler @CompileFlags /TC /std:c17 ('/Fe'+(Join-Path $out ($name+'.exe'))) ('/Fo'+(Join-Path $out ($name+'.obj'))) (Join-Path $PSScriptRoot ('client\'+$name+'.c')) /link @LinkFlags advapi32.lib
    if ($LASTEXITCODE -ne 0) { throw "Native build failed: $name" }
}
& $Compiler @CompileFlags /std:c++17 /EHsc /MT ('/Fe'+(Join-Path $PSScriptRoot 'HiddenTask.exe')) ('/Fo'+(Join-Path $out 'HiddenTask.obj')) (Join-Path $PSScriptRoot 'client\HiddenTask.cpp') /link @LinkFlags /SUBSYSTEM:WINDOWS
if ($LASTEXITCODE -ne 0) { throw 'Hidden launcher build failed.' }
& $Compiler @CompileFlags /std:c++17 /EHsc /MT /utf-8 ('/Fe'+(Join-Path $PSScriptRoot 'PowerRibbon.exe')) ('/Fo'+(Join-Path $out 'PowerRibbon.obj')) (Join-Path $PSScriptRoot 'extras\PowerRibbon.cpp') /link @LinkFlags /SUBSYSTEM:WINDOWS
if ($LASTEXITCODE -ne 0) { throw 'Charging animation build failed.' }
$runtimeRoot = Join-Path $env:ProgramFiles 'dotnet\shared\Microsoft.NETCore.App'
$runtime = Get-ChildItem -LiteralPath $runtimeRoot -Directory | Where-Object {$_.Name -match '^8\.'} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
if (-not $runtime -or -not (Test-Path -LiteralPath $Csc)) { throw '.NET 8 and the Visual Studio C# compiler are required.' }
$references = Get-ChildItem -LiteralPath $runtime.FullName -Filter '*.dll' | Where-Object {try{[void][Reflection.AssemblyName]::GetAssemblyName($_.FullName);$true}catch{$false}} | ForEach-Object {'/r:'+$_.FullName}
& $Csc /nologo /nostdlib+ /target:library /platform:x64 /langversion:latest /optimize+ ('/out:'+(Join-Path $PSScriptRoot 'support\VerifiedPowerSource.dll')) @references (Join-Path $PSScriptRoot 'support\VerifiedPowerSource.cs') (Join-Path $PSScriptRoot 'support\ChargerSource.cs') (Join-Path $PSScriptRoot 'support\ChargingRibbonPolicy.cs')
if ($LASTEXITCODE -ne 0) { throw 'Power source helper build failed.' }
'Build complete. No driver was loaded and no hardware was accessed.'
