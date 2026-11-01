<#
Tools/adsk_test_win64.ps1 - run the AdamskAI automation tests headlessly. The Win64 twin of
Tools/adsk_test.sh: same filter, report directory, verdict and exit codes.

    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_test_win64.ps1
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_test_win64.ps1 AdamskAI.Host.
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_test_win64.ps1 -Standalone -Project D:\MyGame

    <filter>      automation test path prefix, default "AdamskAI."
    -Standalone   skip the project's Tools\run_tests_win64.ps1 even when it exists

With a project Tools\run_tests_win64.ps1, this delegates to it and returns its exit code unchanged.
Otherwise it runs `Automation RunTests <filter>` then `Automation Quit` in a fresh report directory
(Saved\AdamskAI\automation, deleted first so a stale report can never read as a pass) and reads the
verdict with Tools\lib\adsk_test_report.py, the same file adsk_test.sh runs: index.json first, the
log's completion markers only as a fallback. Full output: Saved\Logs\adsk_test.log. Build the editor
target first (Tools\adsk_build_win64.ps1).

THE TRAILING COMMA IN -ExecCmds IS LOAD-BEARING. -ExecCmds splits on commas, not semicolons, and the
value swallows the arguments after it; `Automation RunTests <f>, Automation Quit,` is the one form
seen to run and exit. That is also why a filter may not contain a comma.

THE EDITOR MAY NOT EXIT after the tests. The run has a hard timeout (ADSK_TEST_TIMEOUT); on expiry the
process tree is killed BY PID and the result is exit 3, never a pass. A filter that matches no test
is exit 1 (UE 5.8 logs `No automation tests matched` and writes no report).

Environment: UE_ROOT, ADSK_TEST_TIMEOUT (seconds, default 900), ADSK_PREFLIGHT.

Exit codes: 0 every executed test passed; 1 a test failed or none ran; 2 usage, project or engine
failure; 3 timeout, crash, or a missing/incomplete completion.
#>
[CmdletBinding()]
param(
	[Parameter(Position = 0)]
	[string] $Filter = 'AdamskAI.',
	[string] $Project,
	[switch] $Standalone
)

# Any script error is a tooling failure (exit 2), never a silent pass.
trap { [Console]::Error.WriteLine("ERROR: $($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber): $_"); exit 2 }
. (Join-Path $PSScriptRoot 'lib\adsk_env_win64.ps1')

if (-not $Filter) { Write-AdskError 'empty filter.'; exit 2 }
if ($Filter.Contains(',')) { Write-AdskError 'filter must not contain a comma: -ExecCmds splits on it.'; exit 2 }
if ($Filter -match '[\r\n"]') { Write-AdskError 'filter must not contain a newline, carriage return or double quote.'; exit 2 }

$Ctx = Resolve-AdskProject $Project
if (-not $Ctx) { exit 2 }

$ProjectRunner = Join-Path $Ctx.ProjectRoot 'Tools\run_tests_win64.ps1'
if (-not $Standalone -and (Test-Path -LiteralPath $ProjectRunner -PathType Leaf)) {
	& powershell -NoProfile -ExecutionPolicy Bypass -File $ProjectRunner $Filter
	exit $LASTEXITCODE
}

if (-not (Resolve-AdskEngine $Ctx)) { exit 2 }
# ASan modules use the ANSI allocator; an installed engine frees their objects through mimalloc and crashes in GC.
$Installed = Test-Path -LiteralPath (Join-Path $Ctx.UERoot 'Engine\Build\InstalledBuild.txt')
$WorldTests = 'AdamskAIWorld.'.StartsWith($Filter) -or $Filter.StartsWith('AdamskAIWorld')
if ($Installed -and $WorldTests) {
	$ModuleBuild = Join-Path $PSScriptRoot '..\Intermediate\Build\Win64'
	$Asan = Get-ChildItem -LiteralPath $ModuleBuild -Recurse -File -Include '*.rsp', 'Definitions.AdamskAI.h' -ErrorAction SilentlyContinue |
		Where-Object { $_.Directory.Name -eq 'AdamskAI' } | Select-String -SimpleMatch 'FORCE_ANSI_ALLOCATOR=1' -List
	if ($Asan) {
		Write-AdskError 'AdamskAIWorld.* cannot run under ASan on an installed engine (mixed allocators; docs/windows.md). Run AdamskAI.* or rebuild without -EnableASan.'
		exit 2
	}
}
$Code = Invoke-AdskPreflight $Ctx
if ($Code -ne 0) { exit $Code }

$ReportDir = Join-Path $Ctx.ProjectRoot 'Saved\AdamskAI\automation'
$LogDir = Join-Path $Ctx.ProjectRoot 'Saved\Logs'
$Log = Join-Path $LogDir 'adsk_test.log'
# The report directory is deleted before every run so a stale one can never read as a pass.
if (Test-Path -LiteralPath $ReportDir) {
	$Item = Get-Item -LiteralPath $ReportDir -Force
	if (-not $Item.PSIsContainer -or ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
		Write-AdskError "$ReportDir is a link or not a directory; refusing to delete it."; exit 2
	}
	Remove-Item -LiteralPath $ReportDir -Recurse -Force -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $ReportDir, $LogDir -ErrorAction SilentlyContinue | Out-Null
if ((Test-Path -LiteralPath (Join-Path $ReportDir '*')) -or -not (Test-Path -LiteralPath $LogDir -PathType Container)) {
	Write-AdskError 'cannot prepare report and log directories.'; exit 2
}

$Python = Get-AdskPython $Ctx.UERoot
if (-not $Python) { Write-AdskError 'Python 3.8+ is required to read the report.'; exit 3 }

Write-Host "Running tests [$Filter]..."
$TimeoutSeconds = if ($env:ADSK_TEST_TIMEOUT) { [int] $env:ADSK_TEST_TIMEOUT } else { 900 }
# Comma-separated as in adsk_test.sh: a semicolon-separated form left the editor running. -stdout is
# needed on Windows for the log to carry the completion markers the fallback verdict reads.
$Run = Invoke-AdskEditor $Ctx @("-ReportExportPath=$ReportDir", '-unattended', '-nullrhi', '-nosplash', '-nopause', '-stdout',
	"-ExecCmds=Automation RunTests $Filter, Automation Quit,") $Log $TimeoutSeconds

# A Windows crash is an NTSTATUS (negative here); the verdict script treats any nonzero status without
# a proven failure as an incomplete run.
$PyArgs = $Python.Args
& $Python.Exe @PyArgs -I (Join-Path $PSScriptRoot 'lib\adsk_test_report.py') (Join-Path $ReportDir 'index.json') $Log $Filter $Run.Status ([int] $Run.TimedOut)
exit $LASTEXITCODE
