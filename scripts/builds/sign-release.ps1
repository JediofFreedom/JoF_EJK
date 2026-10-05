#Requires -Version 5.1
<#
Creates a new signed ZIP from a complete, installed Windows release directory.
Never modifies the input. DLLs embedded in PK3s are signed before repacking.
See documentation/developer/windows-signing.md for publisher setup and testing.
#>
[CmdletBinding(DefaultParameterSetName = 'Certificate')]
param(
    [Parameter(Mandatory)][string]$ReleaseDirectory,
    [Parameter(Mandatory)][string]$OutputZip,
    [Parameter(Mandatory, ParameterSetName = 'Certificate')]
    [ValidatePattern('^[a-fA-F0-9]{40}$')][string]$CertificateThumbprint,
    [Parameter(ParameterSetName = 'Certificate')][switch]$MachineStore,
    [Parameter(Mandatory, ParameterSetName = 'Artifact')][string]$SigningDlib,
    [Parameter(Mandatory, ParameterSetName = 'Artifact')][string]$SigningMetadata,
    [string]$SignTool = 'signtool.exe',
    [string]$TimestampUrl
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Invoke-SignTool([string[]]$ToolArguments) {
    & $script:SignTool @ToolArguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "SignTool failed ($LASTEXITCODE): $($ToolArguments[0])" }
}
function Sign-Binary([string]$Path) {
    # Keep valid third-party signatures; do not impersonate dependency publishers.
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or -not $signature.TimeStamperCertificate) {
        Invoke-SignTool ($script:signArguments + @($Path))
    }
    Invoke-SignTool @('verify', '/pa', '/all', '/tw', $Path)
}

$source = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw 'ReleaseDirectory must be a directory.' }
if (@(Get-ChildItem -LiteralPath $source -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
    throw 'Release inputs must not contain links.'
}
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputZip)
if ([IO.Path]::GetExtension($output) -ne '.zip') { throw 'OutputZip must end in .zip.' }
if (Test-Path -LiteralPath $output) { throw 'OutputZip already exists; choose a new filename.' }
if ($output.StartsWith($source.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputZip must be outside ReleaseDirectory.'
}
$script:SignTool = (Get-Command $SignTool -ErrorAction Stop).Source
if (-not $TimestampUrl) {
    $TimestampUrl = if ($PSCmdlet.ParameterSetName -eq 'Artifact') { 'http://timestamp.acs.microsoft.com' } else { 'http://timestamp.digicert.com' }
}
$script:signArguments = @('sign', '/fd', 'SHA256', '/tr', $TimestampUrl, '/td', 'SHA256')
if ($PSCmdlet.ParameterSetName -eq 'Artifact') {
    $dlib = (Resolve-Path -LiteralPath $SigningDlib).Path
    $metadata = (Resolve-Path -LiteralPath $SigningMetadata).Path
    $script:signArguments += @('/dlib', $dlib, '/dmdf', $metadata)
} else {
    $store = if ($MachineStore) { 'LocalMachine' } else { 'CurrentUser' }
    $cert = Get-Item -LiteralPath "Cert:\$store\My\$CertificateThumbprint"
    if (-not $cert.HasPrivateKey -or $cert.NotAfter -lt (Get-Date)) { throw 'A current signing certificate with its private key is required.' }
    $script:signArguments += @('/sha1', $CertificateThumbprint, '/s', 'My')
    if ($MachineStore) { $script:signArguments += '/sm' }
}

$work = Join-Path ([IO.Path]::GetTempPath()) ('jof-sign-' + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $work 'payload'
$candidate = Join-Path $work 'release.zip'
$manifest = [Collections.Generic.List[object]]::new()
try {
    New-Item -ItemType Directory -Path $payload | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $source -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Release inputs must not contain links.' }
        Copy-Item -LiteralPath $item.FullName -Destination $payload -Recurse -Force
    }
    $binaries = @(Get-ChildItem -LiteralPath $payload -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' })
    if (-not @($binaries | Where-Object Extension -eq '.exe').Count) { throw 'Release contains no executable.' }
    foreach ($file in $binaries) {
        Sign-Binary $file.FullName
        $manifest.Add(@{ File = $file.FullName.Substring($payload.Length + 1); SHA256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash })
    }
    foreach ($pk3 in Get-ChildItem -LiteralPath $payload -Recurse -File -Filter '*.pk3') {
        $archive = [IO.Compression.ZipFile]::Open($pk3.FullName, 'Update')
        try {
            foreach ($entry in @($archive.Entries)) {
                if ([IO.Path]::GetExtension($entry.FullName) -notin '.dll', '.exe') { continue }
                $name = $entry.FullName
                $timestamp = $entry.LastWriteTime
                # Never use an archive entry's path as a filesystem destination.
                $binary = Join-Path $work ([guid]::NewGuid().ToString('N') + [IO.Path]::GetExtension($name))
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $binary)
                Sign-Binary $binary
                $signedHash = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
                $entry.Delete()
                $replacement = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $binary, $name)
                $replacement.LastWriteTime = $timestamp
                $manifest.Add(@{ File = $pk3.FullName.Substring($payload.Length + 1) + '::' + $name; SHA256 = $signedHash })
            }
        } finally { $archive.Dispose() }
        # Reopen the finished PK3 and verify precisely the bytes the loader sees,
        # including its .tmp extension. This catches repacking damage.
        $archive = [IO.Compression.ZipFile]::OpenRead($pk3.FullName)
        try {
            foreach ($entry in $archive.Entries) {
                if ([IO.Path]::GetExtension($entry.FullName) -notin '.dll', '.exe') { continue }
                $binary = Join-Path $work ([guid]::NewGuid().ToString('N') + '.tmp')
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $binary)
                Invoke-SignTool @('verify', '/pa', '/all', '/tw', $binary)
                $key = $pk3.FullName.Substring($payload.Length + 1) + '::' + $entry.FullName
                $expected = @($manifest | Where-Object { $_.File -ceq $key })
                if ($expected.Count -ne 1 -or (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash -ne $expected[0].SHA256) {
                    throw "PK3 content mismatch or duplicate entry: $key"
                }
            }
        } finally { $archive.Dispose() }
    }
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $payload 'signed-binaries.json') -Encoding UTF8
    [IO.Compression.ZipFile]::CreateFromDirectory($payload, $candidate)
    # Move without overwrite only after every signing and verification step succeeds.
    [IO.File]::Move($candidate, $output)
    Write-Host "Signed release: $output"
    Write-Host "SHA256: $((Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash)"
} finally {
    $resolvedWork = [IO.Path]::GetFullPath($work)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedWork.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($resolvedWork) -notmatch '^jof-sign-[a-f0-9]{32}$') {
        throw 'Refusing cleanup outside the generated temporary signing directory.'
    }
    if (Test-Path -LiteralPath $resolvedWork) { Remove-Item -LiteralPath $resolvedWork -Recurse -Force }
}
