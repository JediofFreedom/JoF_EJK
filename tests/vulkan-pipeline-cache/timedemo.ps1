param(
 [Parameter(Mandatory)][string]$BinaryDirectory,
 [Parameter(Mandatory)][string]$GameData,
 [Parameter(Mandatory)][string]$OutputDirectory,
 [switch]$Record,
 [int]$Runs=3
)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path "$OutputDirectory/EternalJK" -Force | Out-Null
$OutputDirectory=(Resolve-Path $OutputDirectory).Path
$BinaryDirectory=(Resolve-Path $BinaryDirectory).Path
$modDir="$OutputDirectory/EternalJK"
Copy-Item "$BinaryDirectory/EternalJK/*.pk3" -Destination $modDir
Copy-Item "$BinaryDirectory/*x86_64.dll" -Destination $modDir
Copy-Item "$GameData/SDL2.x86_64.dll" -Destination $BinaryDirectory
Set-Content -LiteralPath "$modDir/autoexec.cfg" -Value '' -Encoding ASCII
function Run-Client($arguments,$name) {
 $process=Start-Process "$BinaryDirectory/eternaljk.x86_64.exe" -WorkingDirectory $OutputDirectory -ArgumentList $arguments -WindowStyle Hidden -PassThru
 if (-not $process.WaitForExit(60000)) { Stop-Process -Id $process.Id; throw "Timed out: $name" }
 Copy-Item "$modDir/qconsole.log" "$OutputDirectory/$name.log"
 if ($process.ExitCode -ne 0) { throw "Failed: $name, exit $($process.ExitCode)" }
}
$common="+safe +set fs_loadpakdlls 0 +set fs_basepath `"$GameData`" +set fs_homepath `"$OutputDirectory`" +set fs_portable 0 +set logfile 2 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1920 +set r_customheight 1080 +set s_useOpenAL 0 +set cg_logChat 0 +set net_port 0 +set sv_pure 0 +set cl_asyncMapLoad 0 +set r_swapInterval 0 +set r_picmip 0 +set r_DynamicGlow 0 +set r_ext_multisample 0 +set r_flares 0 +set r_dlightMode 0 +set r_ext_max_anisotropy 16"
# Match vanilla's lighting/flares and texture quality for this comparison.
# This benchmark does not select the player's normal graphics settings.
if ($Record) {
 @('set com_maxfps 125','set com_maxfpsUnfocused 125','set com_maxfpsMinimized 125','set sv_maxclients 1','devmap T1_Fatal','wait 125','team free','wait 125','ui_openmenu none','set cl_paused 0','set cg_thirdPerson 1','wait 125','screenshot_png timedemo-record','wait 2','record vulkan-benchmark','+right','wait 5000','-right','stoprecord','echo DEMO_RECORDED','quit') | Set-Content "$modDir/timedemo-record.cfg" -Encoding ASCII
 Run-Client "$common +set cl_renderer rd-vulkan +set cl_rendererFallback 0 +set r_vbo 1 +set r_vbo_models 1 +exec timedemo-record.cfg" 'record'
 Get-ChildItem "$modDir/demos/vulkan-benchmark*" | Select-Object Name,Length
 exit
}
$results=@()
foreach ($run in 1..$Runs) {
 $variants=@('vanilla','vulkan-stream','vulkan-world','vulkan-models','vulkan-buffered')
 if ($run % 2 -eq 0) { [Array]::Reverse($variants) }
 foreach ($variant in $variants) {
  $renderer=if($variant -eq 'vanilla'){'rd-eternaljk'}else{'rd-vulkan'}
  $world=if($variant -in @('vulkan-buffered','vulkan-world')){1}else{0}
  $models=if($variant -in @('vulkan-buffered','vulkan-models')){1}else{0}
  @('set cg_thirdPerson 1','set timedemo 1','demo vulkan-benchmark','wait 900','echo TIMEDEMO_COMPLETE','quit') | Set-Content "$modDir/timedemo-run.cfg" -Encoding ASCII
  Run-Client "$common +set cl_renderer $renderer +set cl_rendererFallback 0 +set r_vbo $world +set r_vbo_models $models +exec timedemo-run.cfg" "$variant-$run"
  $log=Get-Content "$OutputDirectory/$variant-$run.log" -Raw
  $match=[regex]::Match($log,'(\d+) frames,\s+([\d.]+) seconds:\s+([\d.]+) fps')
  if (-not $match.Success -or -not $log.Contains('TIMEDEMO_COMPLETE')) { throw "No timedemo result: $variant-$run" }
  $result=[pscustomobject]@{Variant=$variant;Run=$run;Frames=[int]$match.Groups[1].Value;Seconds=[double]$match.Groups[2].Value;FPS=[double]$match.Groups[3].Value}
  $results+=$result
  $result | ConvertTo-Json -Compress
 }
}
$results | ConvertTo-Json | Set-Content "$OutputDirectory/results.json"
