<#
Tools/adsk_simulate_win64.ps1 - run behavior scenarios headlessly: each JSON scenario drives one behavior file with
fixed steps, its inputs and events at their times, and host or project leaves finishing as it says. The Win64 twin of
Tools/adsk_simulate.sh. Format and findings: docs\dev\testing.md, "Simulation".

    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_simulate_win64.ps1
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_simulate_win64.ps1 Tests\AdamskAI\guard.json

    <scenarios>   files or directories, joined with +; by default the plugin's Fixtures\scenarios and, when it exists,
                  the project's Tests\AdamskAI

A thin wrapper over `adsk_validate_win64.ps1 -Simulate`: same log (Saved\Logs\adsk_validate_simulate.log), output
and exit codes (0 clean; 1 a livelock, hold, failed expectation or compile diagnostic; 2 usage or a scenario that
cannot run; 3 crash or timeout). Each run's trace is Saved\AdamskAI\simulate\<scenario>.trace.
#>
[CmdletBinding()]
param(
	[Parameter(Position = 0)] [string] $Scenarios,
	[string] $Project
)

trap { [Console]::Error.WriteLine("ERROR: $($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber): $_"); exit 2 }
. (Join-Path $PSScriptRoot 'lib\adsk_env_win64.ps1')

if (-not $Scenarios) {
	$Ctx = Resolve-AdskProject $Project
	if (-not $Ctx) { exit 2 }
	$Defaults = @((Join-Path $Ctx.PluginDir 'Fixtures\scenarios'), (Join-Path $Ctx.ProjectRoot 'Tests\AdamskAI')) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }
	$Scenarios = $Defaults -join '+'
}
$Forward = @{ Simulate = $Scenarios }
if ($Project) { $Forward.Project = $Project }
& (Join-Path $PSScriptRoot 'adsk_validate_win64.ps1') @Forward
exit $LASTEXITCODE
