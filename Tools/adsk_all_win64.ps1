<#
Tools/adsk_all_win64.ps1 - the usual loop after a change in one run: build, check, validate, schema, simulate, test. The Win64
twin of Tools/adsk_all.sh (which has no build step: Linux builds with the host project's script).

    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_all_win64.ps1
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_all_win64.ps1 -NoBuild -World

    -NoBuild    skip adsk_build (the editor target is already built)
    -World      also run the AdamskAIWorld.* tests (about a minute more)
    -Filter     the AdamskAI test filter, default "AdamskAI."
    -Project    passed to every step

A failed build stops the run; any other failed step is reported and the next one still runs. Each step prints its own
output, then one summary table lists every step with its exit code and time. Logs are each step's own.

Exit codes: the worst step's, ranked 3 > 2 > 1 > 0 (0 every step clean; see each script for 1, 2 and 3).
#>
[CmdletBinding()]
param(
	[string] $Project,
	[switch] $NoBuild,
	[switch] $World,
	[string] $Filter = 'AdamskAI.'
)

trap { [Console]::Error.WriteLine("ERROR: $($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber): $_"); exit 2 }

$Common = @()
if ($Project) { $Common = @('-Project', $Project) }
$Steps = @()
if (-not $NoBuild) { $Steps += , @('build', 'adsk_build_win64.ps1', $Common) }
$Steps += , @('check', 'adsk_check_win64.ps1', @())
$Steps += , @('validate', 'adsk_validate_win64.ps1', $Common)
$Steps += , @('schema', 'adsk_schema_check_win64.ps1', $Common)
$Steps += , @('simulate', 'adsk_simulate_win64.ps1', $Common)
$Steps += , @("test $Filter", 'adsk_test_win64.ps1', @($Filter) + $Common)
if ($World) { $Steps += , @('test AdamskAIWorld.', 'adsk_test_win64.ps1', @('AdamskAIWorld.') + $Common) }

$Rank = @{ 0 = 0; 1 = 1; 2 = 2; 3 = 3 }
$Worst = 0
$Results = @()
foreach ($Step in $Steps) {
	$Name, $Script, $Arguments = $Step
	Write-Host "==== adsk_all: $Name ($Script $($Arguments -join ' '))"
	$Watch = [Diagnostics.Stopwatch]::StartNew()
	& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $Script) @Arguments
	$Code = $LASTEXITCODE
	$Watch.Stop()
	# A code outside the contract (a crashed host) counts as 3.
	if (-not $Rank.ContainsKey($Code)) { $Code = 3 }
	if ($Rank[$Code] -gt $Rank[$Worst]) { $Worst = $Code }
	$Results += [pscustomobject]@{ Step = $Name; Exit = $Code; Seconds = [int]$Watch.Elapsed.TotalSeconds }
	if ($Name -eq 'build' -and $Code -ne 0) { Write-Host 'adsk_all: the build failed; the other steps need it, so they are skipped.'; break }
}

Write-Host '==== adsk_all: summary'
$Results | Format-Table -AutoSize | Out-String -Width 120 | Write-Host
Write-Host ($(if ($Worst -eq 0) { 'adsk_all: PASS' } else { "adsk_all: FAIL (exit $Worst)" }))
exit $Worst
