function Invoke-HiddenNative {
    param([Parameter(Mandatory)][string]$FilePath,[string[]]$NativeArguments=@())
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=$FilePath
    $info.UseShellExecute=$false
    $info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true
    $info.RedirectStandardError=$true
    $info.WorkingDirectory=$PSScriptRoot
    foreach($argument in $NativeArguments){$info.ArgumentList.Add($argument)}
    $process=[Diagnostics.Process]::Start($info)
    try {
        # Service-control success text is noise; retain it only on failure.
        $quietService=[IO.Path]::GetFileName($FilePath) -ieq 'sc.exe'
        $serviceOutput=[Collections.Generic.List[string]]::new()
        $pending=[Collections.Generic.List[object]]::new()
        $pending.Add(@{Reader=$process.StandardOutput;Task=$process.StandardOutput.ReadLineAsync()})
        $pending.Add(@{Reader=$process.StandardError;Task=$process.StandardError.ReadLineAsync()})
        while($pending.Count){
            [Threading.Tasks.Task[]]$tasks=@($pending | ForEach-Object {$_.Task})
            $index=[Threading.Tasks.Task]::WaitAny($tasks)
            $entry=$pending[$index];$line=$entry.Task.GetAwaiter().GetResult()
            if($null -eq $line){$pending.RemoveAt($index)}
            else {
                if($quietService){$serviceOutput.Add($line)}else{Write-Output $line}
                $entry.Task=$entry.Reader.ReadLineAsync()
            }
        }
        $process.WaitForExit()
        $global:LASTEXITCODE=$process.ExitCode
        if($quietService -and $process.ExitCode -ne 0){$serviceOutput | Write-Output}
    } finally {$process.Dispose()}
}
