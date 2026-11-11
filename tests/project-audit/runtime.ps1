param(
    [Parameter(Mandatory)][string]$BinaryDirectory,
    [Parameter(Mandatory)][string]$GameData,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$DemoFile = '',
    [string]$SeedVulkanCacheDirectory = '',
    [switch]$Record,
    [switch]$LegacyCompletion,
    [ValidateRange(1,20)][int]$Runs = 3,
    [ValidateSet(0,1,2)][int]$RuntimeProfile = 0,
    [ValidateSet(0,1,2)][int]$CgameProfile = 0,
    [ValidateSet(0,1)][int]$SoundProfile = 0,
    [ValidateRange(1000,100000)][int]$MinimumFrames = 1000
)
$ErrorActionPreference = 'Stop'
$binary = (Resolve-Path -LiteralPath $BinaryDirectory).Path
$gameData = (Resolve-Path -LiteralPath $GameData).Path
if (-not $Record -and -not $DemoFile) { throw 'Supply a recorded DemoFile, or use -Record.' }
if ($DemoFile) { $DemoFile = (Resolve-Path -LiteralPath $DemoFile).Path }
$results = @()
$frameCount = $null
$count = if ($Record) { 1 } else { $Runs }
foreach ($run in 1..$count) {
    $homePath = Join-Path $OutputDirectory "run-$run"
    $mod = Join-Path $homePath 'EternalJK'
    if (Test-Path "$mod/qconsole.log") { throw "Use a fresh output directory: $homePath" }
    New-Item -ItemType Directory -Path "$mod/demos" -Force | Out-Null
    $homePath = (Resolve-Path -LiteralPath $homePath).Path
    $mod = Join-Path $homePath 'EternalJK'
    Copy-Item "$binary/EternalJK/*.pk3", "$binary/*x86_64.dll" -Destination $mod
    Copy-Item "$gameData/SDL2.x86_64.dll" -Destination $binary
    Set-Content "$mod/autoexec.cfg" '' -Encoding ASCII
    if ($SeedVulkanCacheDirectory) {
        New-Item -ItemType Directory -Path "$mod/cache/vulkan" -Force | Out-Null
        Copy-Item "$SeedVulkanCacheDirectory/*" -Destination "$mod/cache/vulkan" -Recurse
    }
    $commands = @('set cg_logChat 0', 'set cl_asyncMapLoad 0', 'set net_port 0', 'set sv_pure 0')
    if ($RuntimeProfile) { $commands += @("set r_runtimeProfile $RuntimeProfile", 'r_runtimeProfileReset') }
    if ($CgameProfile) { $commands += @("set cg_runtimeProfile $CgameProfile") }
    if ($SoundProfile) { $commands += @("set s_runtimeProfile $SoundProfile", 's_runtimeProfileReset') }
    if ($Record) {
        $commands += @('set com_maxfps 125', 'set com_maxfpsUnfocused 125', 'set com_maxfpsMinimized 125',
            'set sv_maxclients 16', 'set bot_enable 1', 'set g_gametype 0', 'set timelimit 0', 'set fraglimit 0',
            'set g_warmup 0', 'devmap mp/ffa3', 'wait 125', 'team free', 'wait 125', 'ui_openmenu none',
            'set cl_paused 0', 'god')
        foreach ($name in @('Alora', 'Cultist', 'Desann', 'Reborn', 'Jedi', 'Tavion', 'Chewbacca')) {
            $commands += "addbot $name 3 free 0"
        }
        # Playback advances 50 ms per rendered frame. Accelerate recording so
        # the replay lasts thousands of frames, rather than a subsecond sample.
        $commands += @('wait 1000', 'ui_openmenu none', 'set cl_paused 0', 'set cg_thirdPerson 1',
            'record project-busy-runtime', '+right', 'set timescale 10', 'wait 6250', '-right',
            'set timescale 1', 'screenshot_png runtime-record', 'wait 2', 'stoprecord',
            'echo BUSY_DEMO_RECORDED', 'quit')
    } else {
        Copy-Item -LiteralPath $DemoFile -Destination "$mod/demos/project-busy-runtime.dm_26"
        $completion = @()
        if ($RuntimeProfile) { $completion += 'r_runtimeProfileReport' }
        $completion += @('echo BUSY_DEMO_COMPLETE', 'quit')
        $completion | Set-Content "$mod/runtime-complete.cfg" -Encoding ASCII
        $commands += @('set com_maxfps 0', 'set com_maxfpsUnfocused 0', 'set com_maxfpsMinimized 0',
            'set timedemo 1', 'set nextdemo "exec runtime-complete.cfg"', 'demo project-busy-runtime')
        if ($LegacyCompletion) {
            # Compatibility with old clients whose demo-completion disconnect
            # aborts before nextdemo. A truncated replay has no result and fails.
            $commands += @('wait 12000', 'exec runtime-complete.cfg')
        }
    }
    $commands | Set-Content "$mod/runtime.cfg" -Encoding ASCII
    $arguments = "+safe +set fs_loadpakdlls 0 +set fs_basepath `"$gameData`" +set fs_homepath `"$homePath`" +set fs_portable 0 +set logfile 2 +set cl_renderer rd-vulkan +set cl_rendererFallback 0 +set r_vbo 1 +set r_vbo_models 1 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1920 +set r_customheight 1080 +set s_useOpenAL 0 +set r_swapInterval 0 +exec runtime.cfg"
    $process = Start-Process "$binary/eternaljk.x86_64.exe" -WorkingDirectory $homePath -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(120000)) { Stop-Process -Id $process.Id; throw "Runtime check timed out: $homePath" }
    $log = Get-Content "$mod/qconsole.log" -Raw
    $marker = if ($Record) { 'BUSY_DEMO_RECORDED' } else { 'BUSY_DEMO_COMPLETE' }
    if ($process.ExitCode -ne 0 -or -not $log.Contains($marker) -or $log.Contains('fallback renderer')) {
        throw "Runtime check failed: $homePath"
    }
    if ($Record) {
        if ($log.Contains('not defined')) { throw 'A requested bot is missing; inspect qconsole.log.' }
        Get-ChildItem "$mod/demos/project-busy-runtime*" | Select-Object FullName, Length
        continue
    }
    $matches = [regex]::Matches($log, '(\d+) frames,\s+([\d.]+) seconds:\s+([\d.]+) fps')
    if ($matches.Count -ne 1) { throw 'Expected one complete timedemo result.' }
    $frames = [int]$matches[0].Groups[1].Value
    if ($frames -lt $MinimumFrames) { throw "Replay too short: $frames frames." }
    if ($null -ne $frameCount -and $frames -ne $frameCount) { throw 'Replay frame counts differ.' }
    $frameCount = $frames
    # Cgame emits its final aggregate report in CG_Shutdown while imports are
    # valid. A post-demo console command may no longer reach a disconnected VM.
    if ($CgameProfile -and -not $log.Contains('CGPROFILE')) { throw 'Cgame runtime report was not produced.' }
    if ($SoundProfile -and -not $log.Contains('SOUNDPROFILE')) { throw 'Sound runtime report was not produced.' }
    $result = [pscustomobject]@{Run=$run; RuntimeProfile=$RuntimeProfile; CgameProfile=$CgameProfile; SoundProfile=$SoundProfile; Frames=$frames; Seconds=[double]$matches[0].Groups[2].Value;
        FPS=[double]$matches[0].Groups[3].Value; DemoSHA256=(Get-FileHash -LiteralPath $DemoFile -Algorithm SHA256).Hash;
        Log="$mod/qconsole.log"}
    $results += $result
    $result | ConvertTo-Json -Compress
}
if (-not $Record) { $results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'results.json') }
