<#
Tools/adsk_buildplugin_win64.ps1 - `RunUAT BuildPlugin -StrictIncludes` for Win64: the plugin built
alone, as a package, with unity and precompiled headers off. No .sh twin; on Linux,
Tools/adsk_host_check.sh is the stand-alone proof.

    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_buildplugin_win64.ps1
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_buildplugin_win64.ps1 `
        -Engine 'E:\UE_5.8' -OutDir D:\AdskPackage

BuildPlugin builds the plugin inside a generated HostProject (editor Development plus game Development
and Shipping), so a pass also proves that nothing in this plugin needs the project it sits in. It is
stricter than a project build in one more way: the generated host uses the latest build settings, which
turn MSVC's shadowing warning C4459 into an error. Run it before a release and whenever Build.cs or
public headers change. About two minutes on a warm machine.

Engine: -Engine, else UE_ROOT, else the launcher/source registration of -Association (default 5.8).

-OUTDIR IS EMPTIED BY BUILDPLUGIN ITSELF, so it must be new, empty, or an earlier package or failed run
of this plugin (AdamskAI.uplugin at its root, or under HostProject\Plugins\AdamskAI); anything else is
refused rather than deleted. Full UAT output goes next to it: <OutDir>.log.

CHECK THE FLAG TOOK EFFECT: a silently ignored -StrictIncludes looks exactly like a pass. UAT must log
"Building with precompiled headers and unity disabled", or the run fails even when UAT exits 0.

Errors are echoed even when they sit in an engine header (MSVC blames the header for a plugin name
that shadows it); the `note: see declaration` line below each names the plugin file.

Exit code: RunUAT's (0 success); 1 when UAT passed but strict mode was not confirmed; 2 when the engine
or plugin is not found, the output directory is unsafe, or an editor is running.
#>
[CmdletBinding()]
param(
	[string] $Plugin,
	[string] $OutDir = (Join-Path $env:TEMP 'AdskPackage'),
	[string] $Engine,
	[string] $Association = '5.8'
)

# Windows PowerShell 5.1 leaves $PSScriptRoot empty inside param() defaults, so derive it here.
if (-not $Plugin) { $Plugin = Join-Path (Split-Path $PSScriptRoot -Parent) 'AdamskAI.uplugin' }
# Any script error is a tooling failure (exit 2), never a silent pass.
trap { [Console]::Error.WriteLine("ERROR: $($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber): $_"); exit 2 }
. (Join-Path $PSScriptRoot 'lib\adsk_env_win64.ps1')

if (-not $Engine) { $Engine = Get-AdskEngineRoot $Association }
$RunUAT = if ($Engine) { Join-Path $Engine 'Engine\Build\BatchFiles\RunUAT.bat' } else { $null }
if (-not $RunUAT -or -not (Test-Path -LiteralPath $RunUAT)) { Write-Host "ABORT: no RunUAT.bat for engine '$Engine' (pass -Engine or set UE_ROOT)"; exit 2 }
if (-not (Test-Path -LiteralPath $Plugin)) { Write-Host "ABORT: no .uplugin at '$Plugin'"; exit 2 }
$Plugin = (Resolve-Path -LiteralPath $Plugin).Path
$OutDir = [IO.Path]::GetFullPath($OutDir)

$PluginDir = Split-Path $Plugin -Parent
if ($OutDir.StartsWith($PluginDir, [StringComparison]::OrdinalIgnoreCase)) { Write-Host "ABORT: -OutDir '$OutDir' is inside the plugin"; exit 2 }
# A finished package holds the .uplugin at its root; a failed run leaves only HostProject\Plugins\<name>.
$PluginLeaf = Split-Path $Plugin -Leaf
$Ours = (Test-Path -LiteralPath (Join-Path $OutDir $PluginLeaf)) -or
	(Test-Path -LiteralPath (Join-Path $OutDir "HostProject\Plugins\$([IO.Path]::GetFileNameWithoutExtension($PluginLeaf))\$PluginLeaf"))
if ((Test-Path -LiteralPath $OutDir) -and (Get-ChildItem -LiteralPath $OutDir -Force | Select-Object -First 1) -and -not $Ours) {
	Write-Host "ABORT: -OutDir '$OutDir' is not empty and is not an earlier package of this plugin; BuildPlugin would empty it."
	exit 2
}

# Any editor, not only this project's: BuildPlugin's host shares the engine's binaries. Report, never kill.
$Running = @(Get-Process UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue)
if ($Running.Count -gt 0) {
	Write-Host 'ABORT: an editor is running; close it first. PIDs:'
	$Running | Select-Object Id, ProcessName | Format-Table -AutoSize
	exit 2
}

$UatArgs = @('BuildPlugin', "-Plugin=$Plugin", "-Package=$OutDir", '-TargetPlatforms=Win64', '-StrictIncludes')
$Log = "$OutDir.log"
Write-Host ('UAT ARGS: ' + ($UatArgs -join ' '))
Write-Host "UAT LOG:  $Log"

$Start = Get-Date
$Code = (Invoke-AdskLogged $RunUAT $UatArgs $Log).Status
$Elapsed = (Get-Date) - $Start

# Every error, even one reported inside an engine header: MSVC blames the header for a plugin name that
# shadows it (C4459), and only the following `note: see declaration` line points at the plugin.
# Warnings only from outside the engine, which emits hundreds of deprecation warnings of its own.
Select-String -LiteralPath $Log -Pattern ':\s+(fatal )?error [A-Z]+\d+|note: see declaration|warning C\d+|BUILD (SUCCESSFUL|FAILED)|AutomationException' |
	Where-Object { $_.Line -notmatch 'warning C\d+' -or $_.Line -notmatch '\\Engine\\Source\\' } |
	Select-Object -First 40 | ForEach-Object { Write-Host $_.Line }
$Strict = Select-String -LiteralPath $Log -Pattern 'Building with precompiled headers and unity disabled' -Quiet

Write-Host ''
Write-Host ('EXIT={0}  STRICT={1}  ELAPSED={2:N0}s  PACKAGE={3}' -f $Code, [bool] $Strict, $Elapsed.TotalSeconds, $OutDir)
if ($Code -eq 0 -and -not $Strict) { Write-Host 'FAIL: UAT passed but never confirmed unity and PCH were off.'; exit 1 }
exit $Code
