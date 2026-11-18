param(
    [Parameter(Mandatory)][string]$BinaryDirectory,
    [Parameter(Mandatory)][string]$GameData,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$ValidationLayerDirectory = '',
    [ValidateSet(0,1)][int]$HostImageCopy = 0,
    [ValidateSet(0,1)][int]$Synchronization2 = 1,
    [ValidateSet(0,1)][int]$UploadBatch = 1,
    [ValidateSet(0,16,32)][int]$TextureBits = 0,
    [int]$TextureCacheMegs = 512,
    [int]$TextureWorkers = -1,
    [int]$TextureDiskCacheMegs = 0,
    [ValidateSet(0,1)][int]$WorkerPrepare = 1,
    [ValidateSet(0,1)][int]$FilesystemIndex = 1,
    [ValidateSet(0,1)][int]$LazyPlayerSpecies = 1
)
$ErrorActionPreference = 'Stop'
$binary = (Resolve-Path -LiteralPath $BinaryDirectory).Path
$gameData = (Resolve-Path -LiteralPath $GameData).Path
$mod = Join-Path $OutputDirectory 'EternalJK'
if ((Test-Path "$mod/qconsole.log") -or (Test-Path "$OutputDirectory/validation.log")) { throw 'Use a fresh output directory.' }
New-Item -ItemType Directory -Path $mod -Force | Out-Null
$runHome = (Resolve-Path -LiteralPath $OutputDirectory).Path
$mod = Join-Path $runHome 'EternalJK'
Copy-Item "$binary/EternalJK/*.pk3", "$binary/*x86_64.dll" -Destination $mod
Set-Content "$mod/autoexec.cfg" '' -Encoding ASCII
@('set com_maxfps 125', 'set com_maxfpsUnfocused 125', 'set com_maxfpsMinimized 125',
  'set cg_logChat 0', 'set cl_asyncMapLoad 0', 'set net_port 0', 'set sv_pure 0', 'set sv_maxclients 1',
  'devmap T1_Fatal', 'wait 125', 'team free', 'set cg_thirdPerson 1', 'wait 125',
  'ui_openmenu none', 'set cl_paused 0', 'wait 60',
  'screenshot_png audit-world', 'wait 10', 'cosmetics hats afro', 'wait 60',
  'ui_openmenu ingame_player', 'wait 500', 'screenshot_png audit-models', 'wait 10',
  'echo AUDIT_CUSTOMIZATION_OPEN', 'ui_openmenu ingame_player2', 'wait 125',
  'screenshot_png audit-species', 'wait 10', 'echo AUDIT_CUSTOMIZATION_READY',
  'ui_openmenu ingame_cosmetics', 'wait 125', 'screenshot_png audit-cosmetics', 'wait 10',
  'vid_restart', 'wait 125', 'ui_openmenu none', 'wait 125', 'screenshot_png audit-restart', 'wait 10',
  'devmap mp/ffa3', 'wait 125', 'echo AUDIT_GPU_COMPLETE', 'quit') |
    Set-Content "$mod/audit-gpu.cfg" -Encoding ASCII
$arguments = "+safe +set fs_loadpakdlls 0 +set fs_basepath `"$gameData`" +set fs_homepath `"$runHome`" +set fs_portable 0 +set logfile 2 +set cl_loadTimings 1 +set r_hostImageCopy $HostImageCopy +set r_vulkanSynchronization2 $Synchronization2 +set r_imageUploadBatch $UploadBatch +set r_texturebits $TextureBits +set r_textureCacheMegs $TextureCacheMegs +set cl_rendererFallback 0 +set r_vbo 1 +set r_vbo_models 1 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 640 +set r_customheight 480 +set s_useOpenAL 0 +exec audit-gpu.cfg"
$environment = @{}
$arguments = "+set r_textureWorkers $TextureWorkers +set r_textureDiskCacheMegs $TextureDiskCacheMegs $arguments"
$arguments = "+set r_textureWorkerPrepare $WorkerPrepare +set fs_lookupIndex $FilesystemIndex +set ui_lazyPlayerSpecies $LazyPlayerSpecies $arguments"
try {
    if ($ValidationLayerDirectory) {
        $layer = (Resolve-Path -LiteralPath $ValidationLayerDirectory).Path
        if (-not (Test-Path "$layer/VkLayer_khronos_validation.json")) { throw 'Validation layer manifest not found.' }
        $settings = @{
            VK_LAYER_PATH = $layer
            VK_INSTANCE_LAYERS = 'VK_LAYER_KHRONOS_validation'
            VK_KHRONOS_VALIDATION_VALIDATE_SYNC = 'true'
            VK_KHRONOS_VALIDATION_LOG_FILENAME = "$runHome/validation.log"
            VK_KHRONOS_VALIDATION_DEBUG_ACTION = 'VK_DBG_LAYER_ACTION_LOG_MSG'
            VK_KHRONOS_VALIDATION_REPORT_FLAGS = 'error,warn,info'
        }
        foreach ($setting in $settings.GetEnumerator()) {
            $environment[$setting.Key] = [Environment]::GetEnvironmentVariable($setting.Key, 'Process')
            [Environment]::SetEnvironmentVariable($setting.Key, $setting.Value, 'Process')
        }
    }
    $process = Start-Process "$binary/eternaljk.x86_64.exe" -WorkingDirectory $runHome -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(120000)) { Stop-Process -Id $process.Id; throw 'GPU smoke test timed out.' }
    $log = Get-Content "$mod/qconsole.log" -Raw
    if ($process.ExitCode -ne 0 -or -not $log.Contains('AUDIT_GPU_COMPLETE') -or
        -not $log.Contains('resident cache shutdown: 0 textures, 0 pages') -or $log.Contains('fallback renderer')) {
        throw 'GPU smoke test failed; inspect the isolated qconsole.log.'
    }
    foreach ($shot in @('audit-world', 'audit-models', 'audit-species', 'audit-cosmetics', 'audit-restart')) {
        if (-not (Test-Path "$mod/screenshots/$shot.png")) { throw "Missing screenshot: $shot" }
    }
    if ($LazyPlayerSpecies) {
        $opened = $log.IndexOf('AUDIT_CUSTOMIZATION_OPEN')
        $ready = $log.IndexOf('AUDIT_CUSTOMIZATION_READY')
        $catalog = $log.IndexOf('LOADPROFILE UI species catalogue:')
        if ($opened -lt 0 -or $ready -le $opened -or $catalog -lt $opened -or $catalog -ge $ready) {
            throw 'Species catalogue must stay lazy until the actual customization menu opens.'
        }
    }
    if ($ValidationLayerDirectory) {
        $validation = Get-Content "$runHome/validation.log" -Raw
        if (-not $validation.Contains('CURRENT-VALIDATION-ENABLED') -or -not $validation.Contains('Synchronization')) {
            throw 'Khronos core/synchronization validation was not enabled.'
        }
        if ($validation -match 'Validation Error|Validation Warning|SYNC-HAZARD') { throw "Vulkan validation reported an issue: $runHome/validation.log" }
    }
    'Native Vulkan passed world/model/cosmetic rendering, equip, video restart, map change and clean resource teardown.'
} finally {
    foreach ($setting in $environment.GetEnumerator()) { [Environment]::SetEnvironmentVariable($setting.Key, $setting.Value, 'Process') }
}
