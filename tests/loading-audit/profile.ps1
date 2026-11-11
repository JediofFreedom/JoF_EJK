param(
    [Parameter(Mandatory)][string]$BinaryDirectory,
    [Parameter(Mandatory)][string]$GameData,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Renderer = 'rd-vulkan',
    [int]$Runs = 3,
    [int]$UploadBatch = 1,
    [int]$TextureCacheMegs = -1,
    [int]$HostImageCopy = -1,
    [int]$TextureWorkers = -1,
    [int]$TextureDiskCacheMegs = 0,
    [ValidateSet(-1,0,1)][int]$TextureSignaturePrefix = -1,
    [ValidateSet(-1,0,1)][int]$FilesystemRestartCache = -1,
    [string]$SeedVulkanCacheDirectory = '',
    [ValidateSet(0,1)][int]$WorkerPrepare = 1,
    [ValidateSet(0,1)][int]$FilesystemIndex = 1,
    [ValidateSet(0,1)][int]$LazyPlayerSpecies = 1
)
$ErrorActionPreference = 'Stop'
$BinaryDirectory = (Resolve-Path -LiteralPath $BinaryDirectory).Path
$GameData = (Resolve-Path -LiteralPath $GameData).Path
$results = @()
foreach ($run in 1..$Runs) {
    $runHome = Join-Path $OutputDirectory "run-$run"
    $mod = Join-Path $runHome 'EternalJK'
    if (Test-Path "$mod/qconsole.log") { throw "Use a fresh output directory: $runHome" }
    New-Item -ItemType Directory -Path $mod -Force | Out-Null
    $runHome = (Resolve-Path -LiteralPath $runHome).Path
    $mod = Join-Path $runHome 'EternalJK'
    Copy-Item "$BinaryDirectory/EternalJK/*.pk3", "$BinaryDirectory/*x86_64.dll" -Destination $mod
    Copy-Item "$GameData/SDL2.x86_64.dll" -Destination $BinaryDirectory
    if ($SeedVulkanCacheDirectory) {
        $cache = Join-Path $mod 'cache/vulkan'
        New-Item -ItemType Directory -Path $cache -Force | Out-Null
        Copy-Item "$SeedVulkanCacheDirectory/*" -Destination $cache -Recurse
    }
    Set-Content "$mod/autoexec.cfg" '' -Encoding ASCII
    @("set r_imageUploadBatch $UploadBatch", 'set com_maxfps 125', 'set com_maxfpsUnfocused 125',
      'set com_maxfpsMinimized 125', 'set cl_asyncMapLoad 0', 'set sv_maxclients 1',
      'set sv_pure 0', 'set net_port 0', 'set cg_logChat 0',
      'devmap T1_Fatal', 'wait 120', 'devmap mp/ffa3', 'wait 120',
      'devmap T1_Fatal', 'wait 120', 'echo AUDIT_COMPLETE', 'quit') |
        Set-Content "$mod/loading-audit.cfg" -Encoding ASCII
    $arguments = "+safe +set fs_loadpakdlls 0 +set fs_basepath `"$GameData`" +set fs_homepath `"$runHome`" +set fs_portable 0 +set logfile 2 +set cl_loadTimings 1 +set cl_renderer $Renderer +set cl_rendererFallback 0 +set r_vbo 1 +set r_vbo_models 1 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1230 +set r_customheight 690 +set s_useOpenAL 0 +exec loading-audit.cfg"
    if ($TextureCacheMegs -ge 0) { $arguments = "+set r_textureCacheMegs $TextureCacheMegs $arguments" }
    if ($HostImageCopy -ge 0) { $arguments = "+set r_hostImageCopy $HostImageCopy $arguments" }
    $arguments = "+set r_textureWorkers $TextureWorkers $arguments"
    $arguments = "+set r_textureDiskCacheMegs $TextureDiskCacheMegs $arguments"
    if ($TextureSignaturePrefix -ge 0) { $arguments = "+set r_textureSignaturePrefix $TextureSignaturePrefix $arguments" }
    if ($FilesystemRestartCache -ge 0) { $arguments = "+set fs_restartCache $FilesystemRestartCache $arguments" }
    $arguments = "+set r_textureWorkerPrepare $WorkerPrepare +set fs_lookupIndex $FilesystemIndex +set ui_lazyPlayerSpecies $LazyPlayerSpecies $arguments"
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process "$BinaryDirectory/eternaljk.x86_64.exe" -WorkingDirectory $runHome -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(45000)) { Stop-Process -Id $process.Id; throw "Audit client timed out: $runHome" }
    $timer.Stop()
    $log = Get-Content "$mod/qconsole.log" -Raw
    if ($process.ExitCode -ne 0 -or -not $log.Contains('AUDIT_COMPLETE') -or $log.Contains('fallback renderer')) {
        throw "Audit failed: $runHome (exit $($process.ExitCode))"
    }
    $frames = @([regex]::Matches($log, 'LOADPROFILE map-handoff-to-first-frame: (\d+) ms') |
        ForEach-Object { [int]$_.Groups[1].Value })
    if ($frames.Count -ne 3) { throw "Expected three active-frame timings: $runHome" }
    $result = [pscustomobject]@{Run=$run; Renderer=$Renderer; TextureCacheMegs=$TextureCacheMegs; HostImageCopy=$HostImageCopy; TextureWorkers=$TextureWorkers; TextureDiskCacheMegs=$TextureDiskCacheMegs; TextureSignaturePrefix=$TextureSignaturePrefix; FilesystemRestartCache=$FilesystemRestartCache; WorkerPrepare=$WorkerPrepare; FilesystemIndex=$FilesystemIndex; LazyPlayerSpecies=$LazyPlayerSpecies; ProcessSeconds=$timer.Elapsed.TotalSeconds; MapMilliseconds=$frames; Log="$mod/qconsole.log"}
    $results += $result
    $result | ConvertTo-Json -Compress
}
$results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'results.json')
