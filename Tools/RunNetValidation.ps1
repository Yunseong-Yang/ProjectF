param([int]$Players = 4, [int]$Port = 17977, [switch]$Packaged, [int]$LagMs = 0, [int]$LossPercent = 0,
    [string]$Map = '/Game/ProjectF/Maps/Tests/L_CoopSystems', [string]$EvidenceFolder = '20261030_Continuation', [string]$Outpost = '',
    [string]$PackageDirectory = 'ProjectF_Development')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = 'C:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if ($PackageDirectory -notmatch '^ProjectF_[A-Za-z0-9_]+$') { throw 'Invalid package directory.' }
if ($Packaged) { $editor = Join-Path $projectRoot "Builds\$PackageDirectory\Windows\ProjectF\Binaries\Win64\ProjectF.exe" }
if (-not (Test-Path -LiteralPath $editor)) { throw "Missing local executable: $editor" }
$binaryHash = (Get-FileHash -LiteralPath $editor -Algorithm SHA256).Hash.ToLowerInvariant()
$project = Join-Path $projectRoot 'ProjectF.uproject'
$protocol = [int][regex]::Match((Get-Content -LiteralPath "$projectRoot/Source/ProjectF/Public/Core/PFTypes.h" -Raw), 'ProtocolVersion\s*=\s*(\d+)').Groups[1].Value
$runId = Get-Date -Format 'yyyyMMdd_HHmmss'
if ($Map -notmatch '^/Game/ProjectF/Maps/[A-Za-z0-9_/]+$' -or $EvidenceFolder -notmatch '^[A-Za-z0-9_]+$') { throw 'Invalid local test map or evidence folder.' }
if ($Outpost -and $Outpost -notmatch '^[A-Za-z0-9_]+$') { throw 'Invalid outpost id.' }
$output = Join-Path $projectRoot "Saved\Implementation\$EvidenceFolder\Net_$runId"
New-Item -ItemType Directory -Force -Path $output | Out-Null
$processes = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
$Players = [Math]::Clamp($Players, 2, 4)

function Read-SharedLog([string]$Path) {
    $stream = [IO.FileStream]::new($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    $reader = [IO.StreamReader]::new($stream)
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}

function Start-TestProcess([string]$Name, [string]$MapOrAddress) {
    $arguments = @($MapOrAddress, '-game', '-unattended', '-nop4', '-nosplash', '-NullRHI', '-nosound',
        '-PFNetSmoke', '-LogCmds="LogTemp Verbose"', "-PFNetExpected=$Players", "-port=$Port", "-PktLag=$LagMs", "-PktLoss=$LossPercent", "-saveddirsuffix=PF_Net_${runId}_$Name", "-abslog=$output\$Name.log")
    if (-not $Packaged) { $arguments = @($project) + $arguments }
    if ($Outpost) { $arguments += "-PFNetOutpost=$Outpost" }
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    $null = $process.Handle # Retain the process handle so ExitCode remains available after exit.
    $processes.Add($process)
    return $process
}

try {
    $server = Start-TestProcess 'Host' "$Map`?listen?PFBuild=$protocol"
    $deadline = [DateTime]::UtcNow.AddSeconds(100)
    $ready = $false
    while ([DateTime]::UtcNow -lt $deadline -and -not $server.HasExited) {
        if (Test-Path -LiteralPath "$output\Host.log") {
            $text = Read-SharedLog "$output\Host.log"
            if ($text.Contains('Game Engine Initialized')) { $ready = $true; break }
        }
        Start-Sleep -Milliseconds 500
        $server.Refresh()
    }
    if (-not $ready) { throw 'The local host did not finish starting.' }
    for ($index = 1; $index -lt $Players; $index++) {
        $null = Start-TestProcess "Client$index" "127.0.0.1:$Port`?PFBuild=$protocol"
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(150)
    while (-not $server.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $server.Refresh()
    }
    $hostLog = Read-SharedLog "$output\Host.log"
    $hostExited = $server.HasExited
    $hostExitCode = $null
    if ($hostExited) { $server.WaitForExit(); $hostExitCode = $server.ExitCode }
    $binaryUnchanged = (Get-FileHash -LiteralPath $editor -Algorithm SHA256).Hash.ToLowerInvariant() -eq $binaryHash
    $passed = $hostExited -and $null -ne $hostExitCode -and $hostExitCode -eq 0 -and $binaryUnchanged -and $hostLog.Contains('PF_NET_RESULT passed=1')
    $result = [ordered]@{ passed = $passed; map = $Map; outpost = $Outpost; players = $Players; packaged = [bool]$Packaged;
        executable = $editor; executable_sha256 = $binaryHash; executable_unchanged = $binaryUnchanged;
        host_exited = $hostExited; host_exit_code = $hostExitCode;
        one_way_lag_ms = $LagMs; packet_loss_percent = $LossPercent; transport = 'Loopback UDP, separate local processes'; output = $output }
    $result | ConvertTo-Json | Set-Content -LiteralPath "$output\result.json" -Encoding utf8
    $hostLog -split "`n" | Where-Object { $_ -cmatch 'PF_NET_(JOIN|SPAWN|SERVER_AMMO|CLIENT_AMMO|MOVEMENT|AI_NAV|EXECUTION|OUTPOST|WORLD_SYNC|RECONNECT|RESULT)' } | ForEach-Object { Write-Output $_.Trim() }
    $result | ConvertTo-Json
    if (-not $passed) { throw 'The local network validation failed. See the retained logs.' }
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -InputObject $process -Force }
    }
}
