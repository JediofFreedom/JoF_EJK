param(
    [Parameter(Mandatory)][string]$ReferenceDirectory,
    [Parameter(Mandatory)][string]$CandidateDirectory,
    [Parameter(Mandatory)][string]$GameData,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Renderer = 'rd-eternaljk',
    [string]$Map = 'mp/ffa3',
    [int]$Runs = 3
)
$ErrorActionPreference = 'Stop'
$results = @()
foreach ($run in 1..$Runs) {
    $variants = @('reference', 'candidate')
    if ($run % 2 -eq 0) { [array]::Reverse($variants) }
    foreach ($variant in $variants) {
        $binaryDir = if ($variant -eq 'reference') { $ReferenceDirectory } else { $CandidateDirectory }
        $binaryDir = (Resolve-Path -LiteralPath $binaryDir).Path
        $runDir = Join-Path $OutputDirectory "$variant-$Renderer-$run"
        $modDir = Join-Path $runDir 'EternalJK'
        New-Item -ItemType Directory -Path $modDir -Force | Out-Null
        $runDir = (Resolve-Path -LiteralPath $runDir).Path
        # The game installation is read as fs_basepath. Test data and config
        # writes go to a separate home; +safe skips the player's config/scripts.
        Copy-Item "$CandidateDirectory/EternalJK/*.pk3" -Destination $modDir
        Copy-Item "$binaryDir/*x86_64.dll" -Destination $modDir
        Copy-Item "$GameData/SDL2.x86_64.dll" -Destination $binaryDir
        $arguments = "+safe +set fs_loadpakdlls 0 +set fs_basepath `"$GameData`" +set fs_homepath `"$runDir`" +set fs_portable 0 +set logfile 2 +set cl_renderer $Renderer +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 640 +set r_customheight 480 +set s_initsound 1 +set s_useOpenAL 0 +set cg_logChat 0 +set net_port 0 +set cl_asyncMapLoad 0 +set sv_pure 0 +set sv_maxclients 1 +devmap $Map +wait 300 +echo PERFORMANCE_COMPLETE +quit"
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $process = Start-Process -FilePath "$binaryDir/eternaljk.x86_64.exe" -WorkingDirectory $runDir -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if (-not $process.WaitForExit(45000)) {
            Stop-Process -Id $process.Id
            throw "Client timed out: $runDir"
        }
        $timer.Stop()
        $logPath = Join-Path $modDir 'qconsole.log'
        $log = Get-Content -LiteralPath $logPath -Raw
        $mapTime = [regex]::Match($log, 'CL_InitCGame:\s+([\d.]+) seconds')
        $rendererDll = "${Renderer}_x86_64.dll"
        if ($process.ExitCode -ne 0 -or -not $mapTime.Success -or
            -not $log.Contains('PERFORMANCE_COMPLETE') -or
            -not $log.Contains($rendererDll) -or $log.Contains('fallback renderer')) {
            throw "Client/renderer/map check failed; inspect $logPath"
        }
        $result = [pscustomobject]@{
            Variant = $variant; Renderer = $Renderer; Run = $run
            Seconds = $timer.Elapsed.TotalSeconds
            MapSeconds = [double]::Parse($mapTime.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
            Log = $logPath
        }
        $results += $result
        $result | ConvertTo-Json -Compress
    }
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory "results-$Renderer.json")
