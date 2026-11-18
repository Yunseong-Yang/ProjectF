param([switch]$Build, [string]$EvidenceFolder = '20261030_Continuation')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($EvidenceFolder -notmatch '^[A-Za-z0-9_]+$') { throw 'Invalid evidence folder.' }
$output = Join-Path $projectRoot "Saved\Implementation\$EvidenceFolder"
New-Item -ItemType Directory -Force -Path $output | Out-Null
if ($Build) {
    & 'C:\UE_5.8\Engine\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe' `
        'C:\UE_5.8\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll' ProjectFEditor Win64 Development `
        "-Project=$projectRoot\ProjectF.uproject" -WaitMutex -NoHotReloadFromIDE -NoUBA "-log=$output\validation-build.log"
    if ($LASTEXITCODE -ne 0) { throw 'Local Editor build failed.' }
}
& 'C:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$projectRoot\ProjectF.uproject" -unattended -nop4 -nosplash -NullRHI -nosound `
    '-saveddirsuffix=PF_Automation' '-ExecCmds=Automation RunTests ProjectF.;Quit' '-TestExit=Automation Test Queue Empty' `
    "-ReportExportPath=$output\Automation" "-abslog=$output\validation-tests.log"
if ($LASTEXITCODE -ne 0) { throw 'ProjectF automation tests failed.' }
$report = Get-Content -LiteralPath "$output\Automation\index.json" -Raw | ConvertFrom-Json
if ($report.failed -ne 0 -or $report.notRun -ne 0 -or ($report.succeeded + $report.succeededWithWarnings) -lt 31) {
    throw 'The automation report has failures or missing required tests.'
}
[ordered]@{ passed = $true; succeeded = $report.succeeded; warnings = $report.succeededWithWarnings; failed = $report.failed } | ConvertTo-Json
