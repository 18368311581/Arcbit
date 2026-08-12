param(
    [Parameter(Mandatory=$true)]
    [string]$InputPath,
    [Parameter(Mandatory=$true)]
    [string]$OutputPath
)

$utf8 = [System.Text.UTF8Encoding]::new($false, $true)
$gbk = [System.Text.Encoding]::GetEncoding(
    936,
    [System.Text.EncoderFallback]::ExceptionFallback,
    [System.Text.DecoderFallback]::ExceptionFallback
)

try {
    $source = [System.IO.File]::ReadAllText($InputPath, $utf8)
    $output = [System.Text.StringBuilder]::new($source.Length)

    foreach ($character in $source.ToCharArray()) {
        if ([int][char]$character -lt 0x80) {
            [void]$output.Append($character)
            continue
        }

        foreach ($byte in $gbk.GetBytes([string]$character)) {
            [void]$output.Append(('\x{0:X2}' -f $byte))
        }
    }

    $generated = [regex]::Replace(
        $output.ToString(),
        '\\x[0-9A-F]{2}(?=[0-9A-Fa-f])',
        '$0""'
    )
    $directory = [System.IO.Path]::GetDirectoryName($OutputPath)
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    [System.IO.File]::WriteAllText($OutputPath, $generated, [System.Text.UTF8Encoding]::new($false))
}
catch {
    Write-Error "GBK UI text generation failed: $($_.Exception.Message)"
    exit 1
}
