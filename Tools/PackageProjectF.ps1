param([ValidateSet('Development', 'Shipping')][string]$Configuration = 'Development',
    [string[]]$AdditionalDevelopmentMaps = @(), [string]$ArchiveName = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$maps = '/Game/ProjectF/Maps/L_MountainFrontier+/Game/ProjectF/Maps/Tests/L_CoopSystems+/Game/ProjectF/Maps/Tests/L_RearExecutionReview'
if ($AdditionalDevelopmentMaps.Count) {
    if ($Configuration -ne 'Development') { throw 'Additional review maps are allowed only in Development.' }
    foreach ($map in $AdditionalDevelopmentMaps) {
        if ($map -notmatch '^/Game/ProjectF/Maps/[A-Za-z0-9_/]+$') { throw 'Invalid review map.' }
        $maps += "+$map"
    }
}
if ($ArchiveName -and $ArchiveName -notmatch '^ProjectF_[A-Za-z0-9_]+$') { throw 'Invalid archive name.' }
if ($Configuration -eq 'Shipping') {
    & python (Join-Path $PSScriptRoot 'validate_release.py')
    if ($LASTEXITCODE -ne 0) { throw 'Release acceptance evidence is incomplete; Shipping packaging was not started.' }
    $approvals = Get-Content -LiteralPath (Join-Path $projectRoot 'Docs/AssetApprovals.json') -Raw | ConvertFrom-Json
    $maps = ($approvals.release_maps | ForEach-Object { $_.package }) -join '+'
}
$engine = 'C:\UE_5.8'
$output = Join-Path $projectRoot ('Builds\' + $(if ($ArchiveName) { $ArchiveName } else { "ProjectF_$Configuration" }))
& "$engine\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$projectRoot\ProjectF.uproject" -noP4 -platform=Win64 `
    "-clientconfig=$Configuration" -build -cook -stage -pak -iostore -archive "-archivedirectory=$output" `
    "-map=$maps" -unattended -utf8output -NoXGE -NoUBA '-ubtargs=-NoHotReloadFromIDE'
exit $LASTEXITCODE
