param([switch]$FullMatrix, [string]$Map = '/Game/ProjectF/Maps/L_MountainFrontier', [string]$EvidenceFolder = '20261030_Continuation',
    [string]$ResultName = 'network-matrix.json', [string]$Outpost = '', [string]$PackageDirectory = 'ProjectF_Development')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($EvidenceFolder -notmatch '^[A-Za-z0-9_]+$') { throw 'Invalid evidence folder.' }
if ($ResultName -notmatch '^[A-Za-z0-9_][A-Za-z0-9_.-]*\.json$') { throw 'Invalid matrix result filename.' }
$cases = @(
    [pscustomobject]@{Lag=0;Loss=0},
    [pscustomobject]@{Lag=40;Loss=1},
    [pscustomobject]@{Lag=75;Loss=3}
)
if ($FullMatrix) {
    $cases = foreach ($lag in @(0,40,75)) { foreach ($loss in @(0,1,3)) { [pscustomobject]@{Lag=$lag;Loss=$loss} } }
}
$results = [System.Collections.Generic.List[object]]::new()
$index = 0
foreach ($case in $cases) {
    $index++
    Write-Output "Running local packaged case $index/$($cases.Count): one-way lag $($case.Lag) ms, loss $($case.Loss)%"
    $caseStarted = Get-Date
    $caseFailure = $null
    try {
        & "$PSScriptRoot\RunNetValidation.ps1" -Players 4 -Port (17980 + $index) -Packaged -Map $Map -EvidenceFolder $EvidenceFolder -LagMs $case.Lag -LossPercent $case.Loss -Outpost $Outpost -PackageDirectory $PackageDirectory
    }
    catch { $caseFailure = $_ }
    $latest = Get-ChildItem -LiteralPath "$root\Saved\Implementation\$EvidenceFolder" -Directory -Filter 'Net_*' |
        Where-Object { $_.CreationTime -ge $caseStarted.AddSeconds(-1) } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $resultFile = if ($latest) { Join-Path $latest.FullName 'result.json' } else { '' }
    $result = if ($resultFile -and (Test-Path -LiteralPath $resultFile)) {
        Get-Content -LiteralPath $resultFile -Raw | ConvertFrom-Json
    } else {
        [pscustomobject]@{ passed = $false; map = $Map; outpost = $Outpost; players = 4; packaged = $true;
            one_way_lag_ms = $case.Lag; packet_loss_percent = $case.Loss; error = [string]$caseFailure }
    }
    $results.Add($result)
    $results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$root\Saved\Implementation\$EvidenceFolder\$ResultName" -Encoding utf8
    if ($caseFailure) { throw $caseFailure }
    if (-not $result.passed) { throw 'A required network matrix case failed.' }
}
Write-Output "All $($results.Count) packaged local network cases passed."
