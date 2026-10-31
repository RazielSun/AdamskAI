<#
Tools/adsk_perf_compare_win64.ps1 - compare the last benchmark numbers with this machine's committed baseline and the
agreed ceilings (docs/dev/perf.md). The Win64 twin of Tools/adsk_perf_compare.sh; both run Tools\lib\adsk_perf_compare.py.

    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_perf_compare_win64.ps1
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_perf_compare_win64.ps1 -Update

Reads <project>\Saved\AdamskAI\perf\*.json (written by adsk_test_win64.ps1 AdamskAI.Perf. and AdamskAIWorld.Perf.),
Fixtures\perf\baseline-<computer name>.json and Fixtures\perf\ceilings.json in the plugin. Prints every number with its
change; a time, byte count or per-agent rate more than 20% higher is marked WORSE, a number above its ceiling OVER
CEILING. -Update writes the current numbers as the baseline: do it deliberately, in the change that explains why.
-Baseline names another baseline file.

Exit codes: 0 compared or baseline written (WORSE and OVER CEILING lines are warnings); 1 one of them and -Strict;
2 usage, no project, no Python, no results or no baseline.
#>
[CmdletBinding()]
param(
	[string] $Project,
	[string] $Baseline,
	[switch] $Update,
	[switch] $Strict
)

trap { [Console]::Error.WriteLine("ERROR: $($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber): $_"); exit 2 }
. (Join-Path $PSScriptRoot 'lib\adsk_env_win64.ps1')

$Ctx = Resolve-AdskProject $Project
if (-not $Ctx) { exit 2 }
$Python = Get-AdskPython $env:UE_ROOT
if (-not $Python) { Write-AdskError 'Python 3.8+ is required (python, py -3, or set UE_ROOT for the engine''s own).'; exit 2 }

$Extra = @()
if ($Baseline) { $Extra += @('--baseline', $Baseline) }
if ($Update) { $Extra += '--update' }
if ($Strict) { $Extra += '--strict' }
$PyArgs = $Python.Args
& $Python.Exe @PyArgs -I (Join-Path $PSScriptRoot 'lib\adsk_perf_compare.py') $Ctx.ProjectRoot $Ctx.PluginDir @Extra
exit $LASTEXITCODE
