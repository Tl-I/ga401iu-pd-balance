$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'No C++ build tools installation found.' }
$msvc = Get-ChildItem -LiteralPath (Join-Path $installation 'VC\Tools\MSVC') -Directory | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
$kit = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdk = Get-ChildItem -LiteralPath (Join-Path $kit 'Include') -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um\windows.h') } | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
if (-not $msvc -or -not $sdk) { throw 'MSVC and the Windows SDK are required.' }
$script:Compiler = Join-Path $msvc.FullName 'bin\Hostx64\x64\cl.exe'
$script:Csc = Join-Path $installation 'MSBuild\Current\Bin\Roslyn\csc.exe'
$script:CompileFlags = @('/nologo','/W4','/WX','/O2','/GS',('/I'+(Join-Path $msvc.FullName 'include')),('/I'+(Join-Path $sdk.FullName 'ucrt')),('/I'+(Join-Path $sdk.FullName 'um')),('/I'+(Join-Path $sdk.FullName 'shared')))
$script:LinkFlags = @(('/libpath:'+(Join-Path $msvc.FullName 'lib\x64')),('/libpath:'+(Join-Path $kit ('Lib\'+$sdk.Name+'\ucrt\x64'))),('/libpath:'+(Join-Path $kit ('Lib\'+$sdk.Name+'\um\x64'))))
