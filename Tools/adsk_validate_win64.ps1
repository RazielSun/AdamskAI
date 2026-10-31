<#
Tools/adsk_validate_win64.ps1 - validate AdamskAI behavior files headlessly (no renderer, no client,
no PIE). The Win64 twin of Tools/adsk_validate.sh: same modes, logs, report and exit codes.

    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_validate_win64.ps1
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_validate_win64.ps1 -Mode fixtures
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_validate_win64.ps1 `
        -Paths D:\MyGame\Content\AI\guard.xml -Project D:\MyGame

    -Mode all (default)  default, fixtures, catalog and xsd, plus the checks of the project's full catalogue
                         <BehaviorRoot>\node-catalog.md and schema <BehaviorRoot>\adamskai.xsd (required once
                         the project registers node providers; skipped with a note when it has none)
    -Mode default        the plugin Examples against the plugin catalogue, then the project's behavior
                         root (Content\<BehaviorRoot>, default Content\AI)
    -Mode fixtures       the plugin's valid/invalid fixtures
    -Mode catalog        check docs\node-catalog.md against the plugin catalogue (never writes)
    -Mode xsd            check docs\adamskai.xsd against the plugin catalogue (never writes)
    -Paths <p[+p]>       validate files or directories against the full catalogue (implies -Mode paths)
    -Simulate <p[+p]>    run scenario files or directories of them (implies -Mode simulate); see
                         Tools\adsk_simulate_win64.ps1, which supplies the default directories

Each check is one `UnrealEditor-Cmd -run=AdamskAIValidate` run; its full log is
Saved\Logs\adsk_validate_<check>.log. Output carries only structured diagnostics
(`path:line: severity code [node]: message`), stale-catalogue lines, each run's summary and a final
PASS/FAIL line. The machine report is Saved\AdamskAI\validate.json. Build the editor target first
(Tools\adsk_build_win64.ps1): the commandlet lives in the AdamskAIEditor module.

-STDOUT IS REQUIRED ON WINDOWS. UnrealEditor-Cmd does not copy its log to stdout there unless asked,
and every verdict below is read from the LogAdamskAIValidate lines of that stdout.

A CRASH IS NEVER A PASS. No `ADSK_VALIDATE_COMPLETE exit=N` line, a timeout (the process tree is
killed by PID), or a negative exit status (an NTSTATUS such as 0xC0000005) all report exit 3.

Environment: UE_ROOT, ADSK_VALIDATE_TIMEOUT (seconds, default 600), ADSK_PREFLIGHT.

Exit codes: 0 clean; 1 diagnostics or a stale catalogue; 2 usage, project, engine or write failure;
3 timeout, crash or a missing `ADSK_VALIDATE_COMPLETE` marker, which is never a pass. Several checks
report the worst code, ranked 3 > 2 > 1 > 0.
#>
[CmdletBinding()]
param(
	[string] $Project,
	[ValidateSet('all', 'default', 'fixtures', 'catalog', 'xsd', 'paths', 'simulate')]
	[string] $Mode = 'all',
	[string] $Paths,
	[string] $Simulate
)

# Any script error is a tooling failure (exit 2), never a silent pass.
trap { [Console]::Error.WriteLine("ERROR: $($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber): $_"); exit 2 }
. (Join-Path $PSScriptRoot 'lib\adsk_env_win64.ps1')

if ($PSBoundParameters.ContainsKey('Paths') -and $PSBoundParameters.ContainsKey('Simulate')) { Write-AdskError 'use -Paths or -Simulate, not both.'; exit 2 }
if ($PSBoundParameters.ContainsKey('Paths')) {
	if (-not $Paths) { Write-AdskError '-Paths needs a non-empty value.'; exit 2 }
	$Mode = 'paths'
}
if ($PSBoundParameters.ContainsKey('Simulate')) {
	if (-not $Simulate) { Write-AdskError '-Simulate needs a non-empty value.'; exit 2 }
	$Mode = 'simulate'
}
if ($Mode -eq 'paths' -and -not $Paths) { Write-AdskError '-Mode paths needs -Paths.'; exit 2 }
if ($Mode -eq 'simulate' -and -not $Simulate) { Write-AdskError '-Mode simulate needs -Simulate.'; exit 2 }
# The editor resolves a relative path against its own binaries folder, so each part is made absolute here, against
# the caller's directory.
function ConvertTo-AbsoluteList([string] $List) {
	(@($List -split '\+' | ForEach-Object {
		if ([IO.Path]::IsPathRooted($_)) { $_ } else { [IO.Path]::GetFullPath((Join-Path (Get-Location).ProviderPath $_)) }
	}) -join '+')
}
if ($Paths) { $Paths = ConvertTo-AbsoluteList $Paths }
if ($Simulate) { $Simulate = ConvertTo-AbsoluteList $Simulate }

$Ctx = Resolve-AdskProject $Project
if (-not $Ctx) { exit 2 }
if (-not (Resolve-AdskEngine $Ctx)) { exit 2 }
$Code = Invoke-AdskPreflight $Ctx
if ($Code -ne 0) { exit $Code }

$LogDir = Join-Path $Ctx.ProjectRoot 'Saved\Logs'
New-Item -ItemType Directory -Force -Path $LogDir -ErrorAction SilentlyContinue | Out-Null
if (-not (Test-Path -LiteralPath $LogDir -PathType Container)) { Write-AdskError "cannot create $LogDir."; exit 2 }
$TimeoutSeconds = if ($env:ADSK_VALIDATE_TIMEOUT) { [int] $env:ADSK_VALIDATE_TIMEOUT } else { 600 }
$Rank = @{ 0 = 1; 1 = 2; 2 = 3; 3 = 4 }
$script:Worst = 0

$Prefix = '^(\[[^\]]*\]\[[^\]]*\])?LogAdamskAIValidate: Display: '

# One commandlet run; prints its structured lines and returns its exit code under the shared rules.
function Invoke-Check([string] $Label, [string[]] $CommandletArgs) {
	$Log = Join-Path $LogDir "adsk_validate_$Label.log"
	$Run = Invoke-AdskEditor $Ctx (@('-run=AdamskAIValidate') + $CommandletArgs + @('-unattended', '-nullrhi', '-nosplash', '-nopause', '-stdout')) $Log $TimeoutSeconds
	$Lines = @(Get-Content -LiteralPath $Log -ErrorAction SilentlyContinue | Where-Object { $_ -match $Prefix } | ForEach-Object { $_ -replace $Prefix, '' })
	$Lines | Where-Object { -not $_.StartsWith('ADSK_VALIDATE_COMPLETE') } | ForEach-Object { Write-Host $_ }
	$Marker = $null
	foreach ($Line in $Lines) { if ($Line -match '^ADSK_VALIDATE_COMPLETE exit=(\d+)') { $Marker = [int] $Matches[1] } }
	$Status = $Run.Status
	if ($Run.TimedOut) { Write-Host "${Label}: timed out after ${TimeoutSeconds}s. Full log: $Log"; return 3 }
	if ($Status -lt 0 -or $Status -gt 255) {
		# A Windows crash exits with an NTSTATUS such as 0xC0000005, read back as a negative number.
		Write-Host ("${Label}: editor crashed (exit 0x{0:X8}). Full log: $Log" -f $Status); return 3
	}
	if ($null -eq $Marker) { Write-Host "${Label}: no ADSK_VALIDATE_COMPLETE marker (crash or the commandlet never ran). Full log: $Log"; return 3 }
	if ($Marker -gt 2 -or $Marker -ne $Status) { Write-Host "${Label}: marker exit=$Marker disagrees with editor status $Status. Full log: $Log"; return 3 }
	return $Marker
}

function Invoke-Recorded([string] $Label, [string[]] $CommandletArgs) {
	$Exit = [int] (Invoke-Check $Label $CommandletArgs)
	if ($Rank[$Exit] -gt $Rank[$script:Worst]) { $script:Worst = $Exit }
}

$Catalog = (Join-Path $Ctx.PluginDir 'docs\node-catalog.md')
$Schema = (Join-Path $Ctx.PluginDir 'docs\adamskai.xsd')
switch ($Mode) {
	'default'  { Invoke-Recorded 'default' @() }
	'fixtures' { Invoke-Recorded 'fixtures' @('-fixtures') }
	'catalog'  { Invoke-Recorded 'catalog' @("-catalog=$Catalog", '-registry=plugin', '-check') }
	'xsd'      { Invoke-Recorded 'xsd' @("-xsd=$Schema", '-registry=plugin', '-check') }
	'paths'    { Invoke-Recorded 'paths' @("-paths=$Paths") }
	'simulate' { Invoke-Recorded 'simulate' @("-simulate=$Simulate") }
	# default, fixtures, and the plugin's and the project's catalogue and schema checks in one editor launch
	# (`-all`). The commandlet resolves the behavior root and whether the project has provider nodes, so a project
	# without any passes without its own catalogue and schema.
	'all'      { Invoke-Recorded 'all' @('-all') }
}

if ($script:Worst -eq 0) { Write-Host "PASS adsk_validate [$Mode]" } else { Write-Host "FAIL adsk_validate [$Mode] exit=$($script:Worst)" }
exit $script:Worst
