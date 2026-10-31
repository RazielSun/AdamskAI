<#
Tools/adsk_strict_includes_win64.ps1 - compile every AdamskAI module with unity and precompiled headers
off, Win64 Development. The Win64 twin of Tools/adsk_strict_includes.sh.

    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_strict_includes_win64.ps1
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_strict_includes_win64.ps1 `
        -Project D:\Host\Host.uproject -EditorTarget MyGameEditor -GameTarget MyGame

WHY A WIN64 RUN AND NOT ONLY THE LINUX ONE. A unity build lets a file borrow an include from whichever
file was concatenated before it; this proves each file includes what it uses. MSVC's two-phase lookup
and warnings differ from clang's (C4459, a name that shadows an engine template parameter, exists only
here), so a Linux pass does not prove this one.

One UBT run per target:module build, `-Module=<m> -DisableUnity -NoPCH -NoSharedPCH`. No tracked file
is touched. The plugin's own modules are rebuilt, the engine's actions stay cached, and the next
normal build recompiles those modules once. NO EDITOR MAY BE RUNNING for this project; the script
lists the PIDs and refuses.

A build whose every compiler error is outside this plugin's Source\ (an engine module or another
plugin of the host that is not strict-clean itself) is BLOCKED, not FAILED: it is listed, does not fail the
run, and proves nothing about this plugin's files in that module. An error reported inside an engine
header counts as this plugin's when a following `note:` line points into Source\.

Exit codes: 0 every build compiled or was blocked; 1 a build failed in this plugin's code (its errors
are printed); 2 usage, project, engine or a running editor.
#>
[CmdletBinding()]
param(
	[string] $Project,
	[string] $EditorTarget,
	[string] $GameTarget
)

# Any script error is a tooling failure (exit 2), never a silent pass.
trap { [Console]::Error.WriteLine("ERROR: $($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber): $_"); exit 2 }
. (Join-Path $PSScriptRoot 'lib\adsk_env_win64.ps1')

$Ctx = Resolve-AdskProject $Project
if (-not $Ctx) { exit 2 }
if (-not (Resolve-AdskEngine $Ctx)) { exit 2 }
$Code = Invoke-AdskPreflight $Ctx
if ($Code -ne 0) { exit $Code }
if (-not (Assert-AdskNoEditor $Ctx)) { exit 2 }

$LogDir = Join-Path $Ctx.ProjectRoot 'Saved\Logs'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$PluginSource = (Join-Path $Ctx.PluginDir 'Source') + '\'
if (-not $EditorTarget) { $EditorTarget = "$($Ctx.Name)Editor" }
if (-not $GameTarget) { $GameTarget = $Ctx.Name }

# The game target is monolithic and carries only Runtime modules (AdamskAIDebug and AdamskAIDemo
# included outside Shipping); Editor and DeveloperTool modules build in the editor target.
$Builds = @(
	"${EditorTarget}:AdamskAI", "${EditorTarget}:AdamskAIHost", "${EditorTarget}:AdamskAIDebug", "${EditorTarget}:AdamskAIDemo",
	"${EditorTarget}:AdamskAIEditor", "${EditorTarget}:AdamskAITests", "${GameTarget}:AdamskAI", "${GameTarget}:AdamskAIHost",
	"${GameTarget}:AdamskAIDebug", "${GameTarget}:AdamskAIDemo"
)

Write-Host "strict-includes: unity OFF, PCH OFF, $($Builds.Count) builds, project $($Ctx.UProject)"
$Failed = $false
$Blocked = 0
foreach ($Build in $Builds) {
	$TargetName, $Module = $Build.Split(':')
	$Log = Join-Path $LogDir "adsk_strict_${TargetName}_$Module.log"
	Write-Host -NoNewline ('  {0,-28} {1,-16} ' -f $TargetName, $Module)
	$Status = (Invoke-AdskLogged $Ctx.BuildBat @($TargetName, 'Win64', 'Development', "-Project=$($Ctx.UProject)", "-Module=$Module",
		'-DisableUnity', '-NoPCH', '-NoSharedPCH', '-WaitMutex', '-NoHotReload') $Log).Status
	$Errors = @(Select-String -LiteralPath $Log -Pattern ':\s+(fatal )?error [A-Z]+\d+' | ForEach-Object { $_.Line })
	if ($Status -eq 0) {
		# UBT can exit 0 without building; the Result line is what it actually says.
		if (Select-String -LiteralPath $Log -Pattern '^Result: Succeeded' -Quiet) { Write-Host 'ok' }
		else { Write-Host "UNCLEAR (no 'Result: Succeeded'). Log: $Log"; $Failed = $true }
		continue
	}
	# Linker errors name no source file; they are this plugin's unless every compile error is elsewhere.
	$Notes = @(Select-String -LiteralPath $Log -Pattern ': note: ' | ForEach-Object { $_.Line })
	$Ours = @(($Errors + $Notes) | Where-Object { $_.StartsWith($PluginSource, [StringComparison]::OrdinalIgnoreCase) -or $_ -match 'error LNK' })
	if ($Errors.Count -eq 0 -or $Ours.Count -gt 0) {
		Write-Host "FAILED. Log: $Log"
		$Shown = if ($Errors.Count -gt 0) { $Errors + $Ours } else { @(Select-String -LiteralPath $Log -Pattern 'Error|^Result:' | ForEach-Object { $_.Line }) }
		$Shown | Select-Object -Unique -First 20 | ForEach-Object { Write-Host "      $_" }
		$Failed = $true
	} else {
		Write-Host "BLOCKED by code outside this plugin. Log: $Log"
		$Errors | Select-Object -First 5 | ForEach-Object { Write-Host "      $_" }
		$Blocked++
	}
}

if ($Failed) { Write-Host 'FAIL adsk_strict_includes'; exit 1 }
if ($Blocked -gt 0) { Write-Host "PASS adsk_strict_includes ($Blocked build(s) blocked by code outside this plugin; see above)"; exit 0 }
Write-Host 'PASS adsk_strict_includes'
exit 0
