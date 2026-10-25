function ConvertTo-NormalDriverPath([string]$Path) {
    $value = $Path.Trim().Trim('"')
    if ($value.StartsWith('\??\', [StringComparison]::Ordinal)) { $value = $value.Substring(4) }
    if ($value -notmatch '^[A-Za-z]:\\') { throw 'Expected an absolute local drive path.' }
    [IO.Path]::GetFullPath($value)
}
