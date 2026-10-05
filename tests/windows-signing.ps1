#Requires -Version 5.1
# Packaging integration test with a fake signer. Does not test Windows trust.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$testRoot = Join-Path $env:TEMP ('jof-sign-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    $source = Join-Path $testRoot 'input'
    New-Item -ItemType Directory -Path $source | Out-Null
    [IO.File]::WriteAllText((Join-Path $source 'client.exe'), 'unsigned-test-exe')
    $archivePath = Join-Path $source 'modules.pk3'
    $archive = [IO.Compression.ZipFile]::Open($archivePath, 'Create')
    try {
        foreach ($name in @('cgame.dll', 'nested/ui.dll', '../outside.dll', 'assets/readme.txt')) {
            $entry = $archive.CreateEntry($name)
            $writer = [IO.StreamWriter]::new($entry.Open())
            try { $writer.Write('original-content') } finally { $writer.Dispose() }
        }
    } finally { $archive.Dispose() }
    $before = (Get-FileHash -LiteralPath $archivePath).Hash
    $fakeTool = Join-Path $testRoot 'fake-signtool.ps1'
    @'
$binary = $args[-1]
if ($env:JOF_SIGN_TEST_FAILURE -eq $args[0]) { exit 1 }
if ($args[0] -eq 'sign') { [IO.File]::AppendAllText($binary, '-signed') }
if ($args[0] -eq 'verify' -and -not [IO.File]::ReadAllText($binary).EndsWith('-signed')) { exit 1 }
exit 0
'@ | Set-Content -LiteralPath $fakeTool
    $dlib = Join-Path $testRoot 'dummy.dll'
    $metadata = Join-Path $testRoot 'metadata.json'
    Set-Content -LiteralPath $dlib -Value 'dummy'
    Set-Content -LiteralPath $metadata -Value '{}'
    $scriptPath = Join-Path $PSScriptRoot '../scripts/builds/sign-release.ps1'
    $options = @{ ReleaseDirectory = $source; SignTool = $fakeTool; SigningDlib = $dlib; SigningMetadata = $metadata }
    $output = Join-Path $testRoot 'release.zip'
    & $scriptPath @options -OutputZip $output
    if (-not (Test-Path -LiteralPath $output)) { throw 'No output ZIP.' }
    if ((Get-FileHash -LiteralPath $archivePath).Hash -ne $before) { throw 'Input archive changed.' }
    if ([IO.File]::ReadAllText((Join-Path $source 'client.exe')) -ne 'unsigned-test-exe') { throw 'Input executable changed.' }
    $unpacked = Join-Path $testRoot 'unpacked'
    [IO.Compression.ZipFile]::ExtractToDirectory($output, $unpacked)
    $manifest = Get-Content -LiteralPath (Join-Path $unpacked 'signed-binaries.json') -Raw | ConvertFrom-Json
    if (@($manifest).Count -ne 4) { throw 'Missing binary inventory entries.' }
    $archive = [IO.Compression.ZipFile]::OpenRead((Join-Path $unpacked 'modules.pk3'))
    try {
        foreach ($entry in $archive.Entries) {
            $reader = [IO.StreamReader]::new($entry.Open())
            try { $content = $reader.ReadToEnd() } finally { $reader.Dispose() }
            $expected = if ($entry.FullName.EndsWith('.dll')) { 'original-content-signed' } else { 'original-content' }
            if ($content -ne $expected) { throw "Wrong archive content: $($entry.FullName)" }
        }
    } finally { $archive.Dispose() }
    foreach ($failure in @('sign', 'verify')) {
        $env:JOF_SIGN_TEST_FAILURE = $failure
        $failedOutput = Join-Path $testRoot "$failure-failed.zip"
        $rejected = $false
        try { & $scriptPath @options -OutputZip $failedOutput } catch { $rejected = $true }
        if (-not $rejected -or (Test-Path -LiteralPath $failedOutput)) { throw "Failure did not prevent publication: $failure" }
    }
    Remove-Item Env:\JOF_SIGN_TEST_FAILURE
    $rejected = $false
    try { & $scriptPath @options -OutputZip $output } catch { $rejected = $true }
    if (-not $rejected) { throw 'Existing output was not rejected.' }
    # Remote signing replaces only binaries matching the original signing request.
    $unsignedRoot = Join-Path $testRoot 'unsigned-remote'
    $signedRoot = Join-Path $testRoot 'signed-remote'
    New-Item -ItemType Directory -Path $unsignedRoot, $signedRoot | Out-Null
    foreach ($name in @('client.exe', 'cgame.dll', 'ui.dll', 'outside.dll')) {
        $original = if ($name -eq 'client.exe') { 'unsigned-test-exe' } else { 'original-content' }
        [IO.File]::WriteAllText((Join-Path $unsignedRoot $name), $original)
        [IO.File]::WriteAllText((Join-Path $signedRoot $name), $original + '-signed')
    }
    $remoteOptions = @{ ReleaseDirectory = $source; SignTool = $fakeTool; SignedBinariesDirectory = $signedRoot; UnsignedBinariesDirectory = $unsignedRoot }
    & $scriptPath @remoteOptions -OutputZip (Join-Path $testRoot 'remote.zip')
    # An unsigned third-party dependency must block packaging, never be re-signed.
    $dependency = Join-Path $source 'third-party.dll'
    [IO.File]::WriteAllText($dependency, 'unsigned-dependency')
    $rejected = $false
    $failedOutput = Join-Path $testRoot 'remote-dependency-failed.zip'
    try { & $scriptPath @remoteOptions -OutputZip $failedOutput } catch { $rejected = $true }
    if (-not $rejected -or (Test-Path -LiteralPath $failedOutput)) { throw 'Unsigned remote dependency was not rejected.' }
    Remove-Item -LiteralPath $dependency
    [IO.File]::WriteAllText((Join-Path $unsignedRoot 'cgame.dll'), 'another-release')
    $rejected = $false
    $failedOutput = Join-Path $testRoot 'remote-mismatch-failed.zip'
    try { & $scriptPath @remoteOptions -OutputZip $failedOutput } catch { $rejected = $true }
    if (-not $rejected -or (Test-Path -LiteralPath $failedOutput)) { throw 'Signing request mismatch was not rejected.' }
    Write-Host 'PASS: local/remote PK3 signing, unchanged inputs/assets, failure handling, overwrite protection, unsigned dependency rejection and request matching.'
} finally {
    Remove-Item Env:\JOF_SIGN_TEST_FAILURE -ErrorAction SilentlyContinue
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $tempPrefix = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($resolved) -notmatch '^jof-sign-test-[a-f0-9]{32}$') { throw 'Unsafe test cleanup path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
