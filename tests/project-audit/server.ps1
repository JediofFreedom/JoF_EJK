param(
    [Parameter(Mandatory)][string]$BinaryDirectory,
    [Parameter(Mandatory)][string]$GameData,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(0,65525)][int]$Port = 0,
    [string]$SourceCommit = '',
    [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
$binary = (Resolve-Path -LiteralPath $BinaryDirectory).Path
$gameData = (Resolve-Path -LiteralPath $GameData).Path
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $out) { throw "Use a new output directory: $out" }
$snapshot = Join-Path $out 'bin'
$serverHome = Join-Path $out 'server'
$observerHome = Join-Path $out 'observer'
$serverMod = Join-Path $serverHome 'EternalJK'
$observerMod = Join-Path $observerHome 'EternalJK'
foreach ($path in @($snapshot,$serverMod,$observerMod)) { New-Item -ItemType Directory -Path $path -Force | Out-Null }
$owned = @()
$reservation = $null
function Read-SharedLog([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    $stream = $null; $reader = $null
    try {
        $stream = [IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        $reader = [IO.StreamReader]::new($stream)
        return $reader.ReadToEnd()
    } catch [IO.IOException] { return '' }
    finally { if ($reader) { $reader.Dispose() } elseif ($stream) { $stream.Dispose() } }
}
function Clean-Log([string]$Text) { return [regex]::Replace($Text,'\^[0-9A-Za-z]','') }
function Section([string]$Text,[string]$Begin,[string]$End) {
    $start = $Text.IndexOf($Begin,[StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Missing marker: $Begin" }
    $finish = $Text.IndexOf($End,$start + $Begin.Length,[StringComparison]::Ordinal)
    if ($finish -le $start) { throw "Missing marker after $Begin`: $End" }
    return $Text.Substring($start + $Begin.Length,$finish-$start-$Begin.Length)
}
function Check-ClientList([string]$Text) {
    $rows = [regex]::Matches($Text,'(?m)^\s*(\d+)\s+F\s+(audit-\d{2})\s+\(bot\)\s*$')
    $names = @($rows | ForEach-Object {$_.Groups[2].Value} | Sort-Object -Unique)
    if ($names.Count -ne 24) { throw "Expected 24 active free-team bots, saw $($names.Count)." }
    for ($i=0;$i -lt 24;$i++) { if (('audit-{0:d2}' -f $i) -notin $names) { throw 'A configured bot is absent.' } }
    if ($Text -notmatch '(?m)^\s*\d+\s+S\s+server-observer\s*$' -or $Text -notmatch 'Listed\s+25\s+clients') {
        throw 'The observer must be a spectator alongside the 24 bots.'
    }
    return $rows
}
function Write-Manifest {
    @($owned | ForEach-Object {[pscustomobject]@{PID=$_.Id;Started=$_.StartTime.ToUniversalTime().ToString('o');Executable=$_.Path}}) |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'owned-processes.json') -Encoding UTF8
}
try {
    foreach ($name in @('eternaljkded.x86_64.exe','eternaljk.x86_64.exe')) {
        Copy-Item -LiteralPath (Join-Path $binary $name) -Destination $snapshot
    }
    foreach ($dll in Get-ChildItem -LiteralPath $binary -Filter '*x86_64.dll' -File) {
        Copy-Item -LiteralPath $dll.FullName -Destination $snapshot
    }
    foreach ($name in @('SDL2.x86_64.dll','OpenAL32.x86_64.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $snapshot $name))) {
            Copy-Item -LiteralPath (Join-Path $gameData $name) -Destination $snapshot
        }
    }
    foreach ($name in @('jampgamex86_64.dll','cgamex86_64.dll','uix86_64.dll','rd-vulkan_x86_64.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $snapshot $name))) { throw "Missing binary: $name" }
    }
    $packages = @(Get-ChildItem -LiteralPath (Join-Path $binary 'EternalJK') -Filter '*.pk3' -File)
    if (-not $packages.Count) { throw 'Expected build-matched mod assets.' }
    foreach ($mod in @($serverMod,$observerMod)) {
        foreach ($pak in $packages) { Copy-Item -LiteralPath $pak.FullName -Destination $mod }
        foreach ($dll in Get-ChildItem -LiteralPath $snapshot -Filter '*x86_64.dll' -File) { Copy-Item -LiteralPath $dll.FullName -Destination $mod }
        foreach ($cfg in @('autoexec.cfg','eternaljk.cfg','jampconfig.cfg')) { Set-Content -LiteralPath (Join-Path $mod $cfg) -Value '' -Encoding ASCII }
    }
    $reservation = [Net.Sockets.Socket]::new([Net.Sockets.AddressFamily]::InterNetwork,[Net.Sockets.SocketType]::Dgram,[Net.Sockets.ProtocolType]::Udp)
    $reservation.ExclusiveAddressUse = $true
    $reservation.Bind([Net.IPEndPoint]::new([Net.IPAddress]::Loopback,$Port))
    $serverPort = $reservation.LocalEndPoint.Port
    $masters = @(1..5 | ForEach-Object {"set sv_master$_ `"`""})
    $serverCommands = @('set sv_maxclients 32','set sv_privateClients 0','set bot_enable 1','set bot_maxbots 0',
        'set g_gametype 0','set g_doWarmup 0','set g_warmup 0','set fraglimit 0','set timelimit 0',
        'set sv_fps 40','set sv_hibernateTime 0','set g_newBotAI 0','set sv_pure 0','set sv_allowDownload 0',
        'set sv_hostname "Private active-bot diagnostic"','set sv_runtimeProfile 1') + $masters + @('map mp/ffa3')
    $botDefinitions = @('Alora','Cultist','Desann','Reborn','Jedi','Tavion','Chewbacca')
    foreach ($i in 0..23) { $serverCommands += ('addbot {0} 3 free 0 audit-{1:d2}' -f $botDefinitions[$i % $botDefinitions.Count],$i) }
    $serverCommands += @('echo ACTIVE24_SERVER_READY','wait 400','echo ACTIVE24_WARMUP_STATUS_BEGIN','status','entitylist',
        'echo ACTIVE24_WARMUP_STATUS_END','sv_runtimeProfileReset','echo ACTIVE24_SAMPLE_BEGIN','wait 800',
        'sv_runtimeProfileReport','echo ACTIVE24_SAMPLE_END','echo ACTIVE24_FINAL_STATUS_BEGIN','status','entitylist',
        'echo ACTIVE24_FINAL_STATUS_END','wait 200','echo ACTIVE24_SERVER_COMPLETE','quit')
    $serverCommands | Set-Content -LiteralPath (Join-Path $serverMod 'active24-server.cfg') -Encoding ASCII
    $control = Join-Path $observerMod 'active24-observer-control.cfg'
    @('vstr active24_poll') | Set-Content -LiteralPath $control -Encoding ASCII
    @('set com_maxfps 125','set com_maxfpsUnfocused 125','set com_maxfpsMinimized 125','set cg_logChat 0',
        'set name server-observer','set cl_asyncMapLoad 0',"connect 127.0.0.1:$serverPort",'wait 500',
        'team spectator','wait 125','follow audit-00','wait 125','ui_openmenu none','set cl_paused 0',
        'echo ACTIVE24_OBSERVER_START_BEGIN','clientlist','screenshot_png active24-start','echo ACTIVE24_OBSERVER_READY',
        'set active24_poll "wait 125; exec active24-observer-control.cfg"','vstr active24_poll') |
        Set-Content -LiteralPath (Join-Path $observerMod 'active24-observer.cfg') -Encoding ASCII
    $masterArgs = (1..5 | ForEach-Object {"+set sv_master$_ `"`""}) -join ' '
    $baseArgs = "+safe +set fs_loadpakdlls 0 +set fs_basepath `"$gameData`" +set fs_game EternalJK +set fs_portable 0 +set logfile 2 +set net_enabled 1 +set net_ip 127.0.0.1 +set net_socksEnabled 0 +set sv_allowDownload 0 $masterArgs"
    $serverArgs = "$baseArgs +set fs_homepath `"$serverHome`" +set dedicated 1 +set net_port $serverPort +set sv_runtimeProfile 1 +exec active24-server.cfg"
    $observerArgs = "$baseArgs +set fs_homepath `"$observerHome`" +set net_port 0 +set cl_updateCheck 0 +set cl_motd 0 +set cl_discordRichPresence 0 +set com_steamIntegration 0 +set cl_allowDownload 0 +set cl_renderer rd-vulkan +set cl_rendererFallback 0 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 640 +set r_customheight 480 +set r_swapInterval 0 +set s_useOpenAL 0 +exec active24-observer.cfg"
    $hashes=@(Get-ChildItem -LiteralPath $snapshot -File | ForEach-Object {[pscustomobject]@{Name=$_.Name;SHA256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
    $plan = [pscustomobject]@{BinaryDirectory=$snapshot;GameData=$gameData;Port=$serverPort;BindAddress='127.0.0.1';Dedicated=1;SourceCommit=$SourceCommit;Binaries=$hashes;
        ServerArguments=$serverArgs;ObserverArguments=$observerArgs;WarmupServerCalls=400;SampleServerCalls=800;
        ConfiguredBots=24;GNewBotAI=0;Download=0;Masters=@('','','','','');WallTimeoutSeconds=50}
    $plan | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'plan.json') -Encoding UTF8
    if ($PrepareOnly) { $plan; return }
    $reservation.Dispose();$reservation=$null
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $server = Start-Process -FilePath (Join-Path $snapshot 'eternaljkded.x86_64.exe') -WorkingDirectory $serverHome -ArgumentList $serverArgs -WindowStyle Hidden -PassThru
    $owned += $server;Write-Manifest
    $observer=$null;$serverLogPath=Join-Path $serverMod 'qconsole.log';$observerLogPath=Join-Path $observerMod 'qconsole.log'
    $endpointChecked=$false;$startSeconds=$null;$endSeconds=$null;$readySeconds=$null;$finishRequested=$false
    while ($timer.Elapsed.TotalSeconds -lt 50) {
        $serverLog = Clean-Log (Read-SharedLog $serverLogPath)
        $observerLog = Clean-Log (Read-SharedLog $observerLogPath)
        if (-not $endpointChecked -and $serverLog.Contains('ACTIVE24_SERVER_READY')) {
            if ($serverLog -notmatch "Opening IP socket: 127\.0\.0\.1:$serverPort\b") { throw 'Server did not open the requested explicit loopback socket.' }
            $endpoints = @(Get-NetUDPEndpoint -OwningProcess $server.Id -ErrorAction Stop)
            if (-not ($endpoints | Where-Object {$_.LocalAddress -eq '127.0.0.1' -and $_.LocalPort -eq $serverPort}) -or
                ($endpoints | Where-Object {$_.LocalAddress -ne '127.0.0.1'})) { throw 'Unexpected server UDP binding.' }
            $endpoints | Select-Object LocalAddress,LocalPort,OwningProcess | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $out 'server-endpoints.json') -Encoding UTF8
            $endpointChecked=$true
            $observer = Start-Process -FilePath (Join-Path $snapshot 'eternaljk.x86_64.exe') -WorkingDirectory $observerHome -ArgumentList $observerArgs -WindowStyle Hidden -PassThru
            $owned += $observer;Write-Manifest
        }
        if ($null -eq $readySeconds -and $observerLog.Contains('ACTIVE24_OBSERVER_READY')) {
            $null=Check-ClientList (Section $observerLog 'ACTIVE24_OBSERVER_START_BEGIN' 'ACTIVE24_OBSERVER_READY')
            $readySeconds=$timer.Elapsed.TotalSeconds
        }
        if ($null -eq $startSeconds -and $serverLog.Contains('ACTIVE24_SAMPLE_BEGIN')) {
            if ($null -eq $readySeconds) { throw 'Observer was not fully active before sampling began.' }
            $startSeconds=$timer.Elapsed.TotalSeconds
        }
        if (-not $finishRequested -and $serverLog.Contains('ACTIVE24_FINAL_STATUS_END')) {
            $endSeconds=$timer.Elapsed.TotalSeconds
            @('echo ACTIVE24_OBSERVER_FINAL_BEGIN','clientlist','+scores','wait 2','screenshot_png active24-end','wait 10','-scores',
                'echo ACTIVE24_OBSERVER_COMPLETE','quit') | Set-Content -LiteralPath $control -Encoding ASCII
            $finishRequested=$true
        }
        $server.Refresh();if ($observer) {$observer.Refresh()}
        if ($server.HasExited -and (!$observer -or $observer.HasExited)) { break }
        if ($server.HasExited -and -not $serverLog.Contains('ACTIVE24_SERVER_COMPLETE')) { throw 'Server exited before completion.' }
        if ($observer -and $observer.HasExited -and -not $observerLog.Contains('ACTIVE24_OBSERVER_COMPLETE')) { throw 'Observer exited before completion.' }
        Start-Sleep -Milliseconds 100
    }
    if (-not $server.HasExited -or -not $observer -or -not $observer.HasExited) { throw 'Local workload exceeded its 50-second wall deadline.' }
    if ($server.ExitCode -ne 0 -or $observer.ExitCode -ne 0) { throw 'An owned game process exited unsuccessfully.' }
    $serverLog=Clean-Log (Read-SharedLog $serverLogPath);$observerLog=Clean-Log (Read-SharedLog $observerLogPath)
    if (-not $serverLog.Contains('ACTIVE24_SERVER_COMPLETE') -or -not $observerLog.Contains('ACTIVE24_OBSERVER_COMPLETE') -or
        $serverLog -match "not defined|Sending heartbeat|Resolving .*master" -or $observerLog -match 'fallback renderer') { throw 'Workload completion/isolation verification failed.' }
    $initial = Section $serverLog 'ACTIVE24_WARMUP_STATUS_BEGIN' 'ACTIVE24_WARMUP_STATUS_END'
    $final = Section $serverLog 'ACTIVE24_FINAL_STATUS_BEGIN' 'ACTIVE24_FINAL_STATUS_END'
    foreach ($status in @($initial,$final)) {
        if ($status -notmatch 'players\s*:\s*1 human,\s*24 bots\(32 max\)' -or
            [regex]::Matches($status,'(?m)^\s*\d+:ET_PLAYER\s+player\s*$').Count -lt 25) { throw 'Expected 24 in-use bot actors plus the observer at both sample boundaries.' }
    }
    $botRows=@(Check-ClientList (Section $observerLog 'ACTIVE24_OBSERVER_START_BEGIN' 'ACTIVE24_OBSERVER_READY'))
    $null=Check-ClientList (Section $observerLog 'ACTIVE24_OBSERVER_FINAL_BEGIN' 'ACTIVE24_OBSERVER_COMPLETE')
    $botIds=@($botRows | ForEach-Object {[int]$_.Groups[1].Value})
    $sample=Section $serverLog 'ACTIVE24_SAMPLE_BEGIN' 'ACTIVE24_SAMPLE_END'
    $kills=@([regex]::Matches($sample,'Kill:\s*(\d+)\s+(\d+)\s+(\d+):[^\r\n]*') |
        Where-Object {[int]$_.Groups[1].Value -in $botIds -and [int]$_.Groups[2].Value -in $botIds -and $_.Groups[1].Value -ne $_.Groups[2].Value})
    if (-not $kills.Count) { throw 'No bot-vs-bot combat kills were observed during sampling; activity is inconclusive.' }
    $stages=@([regex]::Matches($sample,'SERVERPROFILE\s+(sv-frame|bot-ai-call|game-tick|client-messages) calls=(\d+) total_ms=([0-9.]+) mean_us=([0-9.]+) max_us=([0-9.]+)') |
        ForEach-Object {[pscustomobject]@{Stage=$_.Groups[1].Value;Calls=[long]$_.Groups[2].Value;TotalMilliseconds=[double]$_.Groups[3].Value;MeanMicroseconds=[double]$_.Groups[4].Value;MaxMicroseconds=[double]$_.Groups[5].Value}})
    if ($stages.Count -ne 4 -or ($stages | Where-Object {$_.Calls -lt 700})) { throw 'Missing/short server profile sample.' }
    foreach ($name in @('active24-start.png','active24-end.png')) {
        if (-not (Test-Path -LiteralPath (Join-Path $observerMod "screenshots/$name"))) { throw "Missing screenshot: $name" }
    }
    $result=[pscustomobject]@{Status='passed';ConfiguredBots=24;VerifiedFreeTeamBots=$botRows.Count;VerifiedInUseActorsAtBoundaries=25;
        BotVersusBotKills=$kills.Count;CombatEvidence=@($kills | Select-Object -First 8 | ForEach-Object {$_.Value});Port=$serverPort;
        ObserverReadySeconds=$readySeconds;SampleBeginSeconds=$startSeconds;SampleEndSeconds=$endSeconds;
        ObservedSampleSeconds=$endSeconds-$startSeconds;WallSeconds=$timer.Elapsed.TotalSeconds;Stages=$stages;
        ServerLog=$serverLogPath;ObserverLog=$observerLogPath;SourceCommit=$SourceCommit;Binaries=$hashes;DiagnosticOnly=$true;StagesOverlap=$true}
    $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'results.json') -Encoding UTF8
    $result | ConvertTo-Json -Depth 6
} catch {
    [pscustomobject]@{Status='failed';Error=$_.Exception.Message;OutputDirectory=$out} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $out 'failure.json') -Encoding UTF8
    throw
} finally {
    if ($reservation) {$reservation.Dispose()}
    foreach ($process in $owned) {
        $process.Refresh()
        if (-not $process.HasExited) {
            $live=Get-Process -Id $process.Id -ErrorAction SilentlyContinue
            if ($live -and $live.StartTime -eq $process.StartTime -and $live.Path -eq $process.Path) {
                $null=$process.CloseMainWindow()
                if (-not $process.WaitForExit(1000)) { Stop-Process -Id $process.Id; $null=$process.WaitForExit(1000) }
            }
        }
    }
}
