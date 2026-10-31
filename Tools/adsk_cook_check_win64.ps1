<#
Tools/adsk_cook_check_win64.ps1 - cook the host project for Windows and check what AdamskAI adds to and keeps out
of the cook. Win64 only; the Linux project checks its own cook.

    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_cook_check_win64.ps1
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_cook_check_win64.ps1 -Project D:\Host -SkipCook

Checks, after a by-the-book `-run=Cook -TargetPlatform=Windows`:
  1. the cook ran AdamskAI's hook: one `LogAdamskAICook: Display: added N package(s)` line and no
     LogAdamskAICook warning or error (a behavior file that does not compile is a warning there);
  2. every package an object path in the project's behavior files names (Content\<BehaviorRoot>\*.xml,
     attribute values like "/Game/...Asset.Asset", /Script/ excluded) is in the cooked output;
  3. nothing of the plugin's test content (/AdamskAI/Tests, setup.md step 7) is in the cooked output.

-SkipCook rechecks the last cook's log and output. Full log: Saved\Logs\adsk_cook_check.log.
Build the editor target first (Tools\adsk_build_win64.ps1); no editor may be running.

Environment: UE_ROOT, ADSK_COOK_TIMEOUT (seconds, default 3600), ADSK_PREFLIGHT.
Exit codes: 0 pass; 1 a check failed; 2 usage, project or engine; 3 the cook crashed, timed out or failed.
#>
[CmdletBinding()]
param(
	[string] $Project,
	[switch] $SkipCook
)

trap { [Console]::Error.WriteLine("ERROR: $($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber): $_"); exit 2 }
. (Join-Path $PSScriptRoot 'lib\adsk_env_win64.ps1')

$Ctx = Resolve-AdskProject $Project
if (-not $Ctx) { exit 2 }
if (-not (Resolve-AdskEngine $Ctx)) { exit 2 }
$Code = Invoke-AdskPreflight $Ctx
if ($Code -ne 0) { exit $Code }

$LogDir = Join-Path $Ctx.ProjectRoot 'Saved\Logs'
New-Item -ItemType Directory -Force -Path $LogDir -ErrorAction SilentlyContinue | Out-Null
$Log = Join-Path $LogDir 'adsk_cook_check.log'
$Cooked = Join-Path $Ctx.ProjectRoot "Saved\Cooked\Windows\$($Ctx.Name)"

if (-not $SkipCook) {
	if (-not (Assert-AdskNoEditor $Ctx)) { exit 2 }
	$TimeoutSeconds = if ($env:ADSK_COOK_TIMEOUT) { [int] $env:ADSK_COOK_TIMEOUT } else { 3600 }
	Write-Host "Cooking $($Ctx.Name) for Windows..."
	$Run = Invoke-AdskEditor $Ctx @('-run=Cook', '-TargetPlatform=Windows', '-unattended', '-nullrhi', '-nosplash', '-nopause', '-stdout') $Log $TimeoutSeconds
	if ($Run.TimedOut) { Write-Host "FAIL adsk_cook_check: the cook timed out after ${TimeoutSeconds}s. Full log: $Log"; exit 3 }
	if ($Run.Status -ne 0) { Write-Host ("FAIL adsk_cook_check: the cook exited 0x{0:X8}. Full log: $Log" -f $Run.Status); exit 3 }
}
if (-not (Test-Path -LiteralPath $Log)) { Write-AdskError "no cook log at $Log; run without -SkipCook."; exit 2 }
if (-not (Test-Path -LiteralPath $Cooked -PathType Container)) { Write-Host "FAIL adsk_cook_check: no cooked output at $Cooked"; exit 3 }

$Failed = 0
function Report([bool] $Ok, [string] $Line) {
	if ($Ok) { Write-Host "ok   $Line" } else { Write-Host "FAIL $Line"; $script:Failed = 1 }
}

# 1. The hook ran and was quiet.
$CookLines = @(Get-Content -LiteralPath $Log | Where-Object { $_ -match 'LogAdamskAICook: ' })
$Added = @($CookLines | Where-Object { $_ -match 'LogAdamskAICook: Display: added (\d+) package' })
Report ($Added.Count -ge 1) "the cook hook ran: $(if ($Added.Count) { ($Added[-1] -replace '^.*LogAdamskAICook: Display: ', '') } else { 'no LogAdamskAICook added line' })"
$Noisy = @($CookLines | Where-Object { $_ -match 'LogAdamskAICook: (Warning|Error)' })
Report ($Noisy.Count -eq 0) "no LogAdamskAICook warning or error$(if ($Noisy.Count) { ': ' + ($Noisy -join ' | ') })"

# 2. What the behavior files name is cooked.
$Root = 'AI'
$GameIni = Join-Path $Ctx.ProjectRoot 'Config\DefaultGame.ini'
if (Test-Path -LiteralPath $GameIni) {
	$Line = Get-Content -LiteralPath $GameIni | Where-Object { $_ -match '^\s*BehaviorRoot\s*=\s*(.+?)\s*$' } | Select-Object -Last 1
	if ($Line -and $Line -match '^\s*BehaviorRoot\s*=\s*(.+?)\s*$') { $Root = $Matches[1] }
}
$BehaviorDir = Join-Path $Ctx.ProjectRoot "Content\$Root"
$Packages = New-Object System.Collections.Generic.SortedSet[string]
foreach ($File in @(Get-ChildItem -LiteralPath $BehaviorDir -Filter *.xml -Recurse -ErrorAction SilentlyContinue)) {
	foreach ($Match in [regex]::Matches((Get-Content -LiteralPath $File.FullName -Raw), '"(/[A-Za-z0-9_]+(/[^"/.]+)+)\.[^"/]+"')) {
		$Package = $Match.Groups[1].Value
		if (-not $Package.StartsWith('/Script/')) { [void] $Packages.Add($Package) }
	}
}
if ($Packages.Count -eq 0) { Write-Host "note no object paths in $BehaviorDir\*.xml; nothing to look for in the cook" }
# A Zen cook (the default) leaves no loose .uasset files; every cook lists its packages, lowercase, in ReferencedSet.txt.
$SetFile = Join-Path $Cooked 'Metadata\ReferencedSet.txt'
$Cooks = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
if (Test-Path -LiteralPath $SetFile) {
	Get-Content -LiteralPath $SetFile | Where-Object { $_ -and -not $_.StartsWith('#') } | ForEach-Object { [void] $Cooks.Add($_.Trim()) }
} else {
	# Loose files: <Cooked>\Content\X\Y.uasset is /Game/X/Y; a plugin's <Cooked>\...\<Mount>\Content\X\Y.uasset is /<Mount>/X/Y.
	foreach ($File in @(Get-ChildItem -LiteralPath $Cooked -Recurse -Filter *.uasset -ErrorAction SilentlyContinue)) {
		$Relative = $File.FullName.Substring($Cooked.Length + 1) -replace '\.uasset$', ''
		if ($Relative -match '^Content\\(.+)$') { [void] $Cooks.Add('/Game/' + $Matches[1].Replace('\', '/')) }
		elseif ($Relative -match '([^\\]+)\\Content\\(.+)$') { [void] $Cooks.Add('/' + $Matches[1] + '/' + $Matches[2].Replace('\', '/')) }
	}
}
Report ($Cooks.Count -gt 0) "the cook lists $($Cooks.Count) package(s)"
foreach ($Package in $Packages) { Report $Cooks.Contains($Package) "cooked $Package" }

# 3. The test content stayed out.
$TestContent = @($Cooks | Where-Object { $_ -like '/AdamskAI/Tests/*' })
Report ($TestContent.Count -eq 0) "no /AdamskAI/Tests content cooked$(if ($TestContent.Count) { ': ' + (($TestContent | Select-Object -First 3) -join ', ') })"

if ($Failed) { Write-Host "FAIL adsk_cook_check. Full log: $Log"; exit 1 }
Write-Host 'PASS adsk_cook_check'
exit 0
