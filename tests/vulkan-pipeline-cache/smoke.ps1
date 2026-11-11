param(
    [Parameter(Mandatory)][string]$BinaryDirectory,
    [Parameter(Mandatory)][string]$GameData,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Map = 'T1_Fatal'
)
$ErrorActionPreference = 'Stop'
$BinaryDirectory = (Resolve-Path -LiteralPath $BinaryDirectory).Path
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory for the cold-cache test.' }
$modDir = Join-Path $OutputDirectory 'EternalJK'
New-Item -ItemType Directory -Path $modDir -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
Copy-Item "$BinaryDirectory/EternalJK/*.pk3" -Destination $modDir
Copy-Item "$BinaryDirectory/*x86_64.dll" -Destination $modDir
Copy-Item "$GameData/SDL2.x86_64.dll" -Destination $BinaryDirectory
Set-Content -LiteralPath "$modDir/autoexec.cfg" -Value '' -Encoding ASCII
$results = @()
foreach ($phase in @('cold', 'warm', 'corrupt', 'disabled')) {
    if ($phase -eq 'corrupt') {
        $cache = @(Get-ChildItem "$modDir/cache/vulkan/*.dat")
        if ($cache.Count -ne 1) { throw 'Expected one GPU cache.' }
        $bytes = [IO.File]::ReadAllBytes($cache[0].FullName)
        $bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 1
        [IO.File]::WriteAllBytes($cache[0].FullName, $bytes)
    }
    $cacheEnabled = if ($phase -eq 'disabled') { 0 } else { 1 }
    $commands = @(
        'wait 120', 'screenshot_png vulkan-world',
        'ui_openmenu ingame_player', 'wait 900', 'screenshot_png vulkan-models', 'wait 2',
        'ui_openmenu ingame_cosmetics', 'wait 120', 'screenshot_png vulkan-cosmetics', 'wait 2',
        'vid_restart', 'wait 120', 'screenshot_png vulkan-restarted', 'wait 2',
        'echo VULKAN_SMOKE_COMPLETE', 'quit'
    )
    $commands | Set-Content -LiteralPath "$modDir/vulkan-smoke.cfg" -Encoding ASCII
    # Keep the command line below MAX_CONSOLE_LINES; longer sequences belong
    # in a cfg. Isolated home and +safe preserve the player's normal settings.
    $arguments = "+safe +set fs_loadpakdlls 0 +set fs_basepath `"$GameData`" +set fs_homepath `"$OutputDirectory`" +set fs_portable 0 +set logfile 2 +set cl_renderer rd-vulkan +set cl_rendererFallback 0 +set r_vbo 1 +set r_vbo_models 1 +set r_pipelineCache $cacheEnabled +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1280 +set r_customheight 720 +set s_useOpenAL 0 +set cg_logChat 0 +set net_port 0 +set cl_asyncMapLoad 0 +set sv_pure 0 +set sv_maxclients 1 +devmap $Map +exec vulkan-smoke.cfg"
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process "$BinaryDirectory/eternaljk.x86_64.exe" -WorkingDirectory $OutputDirectory -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(60000)) {
        Stop-Process -Id $process.Id
        throw "Vulkan smoke timed out: $phase"
    }
    $timer.Stop()
    $log = Get-Content -LiteralPath "$modDir/qconsole.log" -Raw
    Copy-Item -LiteralPath "$modDir/qconsole.log" -Destination "$OutputDirectory/$phase.log"
    $seeds = @([regex]::Matches($log, 'pipeline cache initialized with (\d+) bytes') | ForEach-Object { [long]$_.Groups[1].Value })
    if ($process.ExitCode -ne 0 -or -not $log.Contains('VULKAN_SMOKE_COMPLETE') -or
        -not $log.Contains('VK_RENDERER:') -or $log.Contains('fallback renderer') -or
        ([regex]::Matches($log, 'vk_shutdown\(\)').Count -ne 2) -or $seeds.Count -ne 2) {
        throw "Native Vulkan/restart check failed: $OutputDirectory/$phase.log"
    }
    if ($phase -eq 'cold' -and ($seeds[0] -ne 0 -or $seeds[1] -le 0)) { throw 'Cold run must save and reuse cache at video restart.' }
    if ($phase -eq 'warm' -and ($seeds[0] -le 0 -or $seeds[1] -le 0)) { throw 'Warm cache was not loaded.' }
    if ($phase -eq 'corrupt' -and ($seeds[0] -ne 0 -or $seeds[1] -le 0 -or -not $log.Contains('ignoring incompatible or incomplete pipeline cache'))) { throw 'Corrupt cache recovery failed.' }
    if ($phase -eq 'disabled' -and ($seeds[0] -ne 0 -or $seeds[1] -ne 0 -or $log.Contains('saved '))) { throw 'Disabled cache still performed disk operations.' }
    $result = [pscustomobject]@{ Phase = $phase; Seconds = $timer.Elapsed.TotalSeconds; CacheSeeds = $seeds; Log = "$OutputDirectory/$phase.log" }
    $results += $result
    $result | ConvertTo-Json -Compress
}
$results | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath "$OutputDirectory/results.json"
