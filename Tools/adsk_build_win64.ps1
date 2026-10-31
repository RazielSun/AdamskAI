<#
Tools/adsk_build_win64.ps1 - build the host project's editor (default) or game target on Win64 with
UnrealBuildTool. Every other adsk_*_win64.ps1 except adsk_check needs a fresh editor build first.
On Linux the host project's own build script plays this role; this script has no .sh twin.

    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_build_win64.ps1
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_build_win64.ps1 -Target Game -Configuration Shipping
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_build_win64.ps1 `
        -Project D:\Host\Host.uproject -TargetName MyGameEditor

The target is <ProjectName>Editor (or <ProjectName> for -Target Game). A project renamed from a
template keeps the template's target names; pass -TargetName then. Extra arguments after the named
ones go to UBT unchanged.

NO EDITOR MAY BE RUNNING for this project: it holds the module DLLs and the link fails on a file it
cannot replace. The script lists the PIDs and refuses; it never kills anything.

UBT CAN EXIT 0 WITHOUT BUILDING, so the verdict is the `Result: Succeeded` line. Full output:
Saved\Logs\adsk_build_<Target>_<Configuration>.log; compiler and linker errors, the `note: see
declaration` lines that point an engine-header error back at plugin code, and the Result line are echoed.

Exit codes: 0 `Result: Succeeded`; 1 compile or link failure; 2 usage, project, engine or a running editor.
#>
[CmdletBinding()]
param(
	[string] $Project,
	[ValidateSet('Editor', 'Game')]
	[string] $Target = 'Editor',
	[string] $Configuration = 'Development',
	[string] $TargetName,
	[Parameter(ValueFromRemainingArguments = $true)]
	[string[]] $ExtraArgs = @()
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

if (-not $TargetName) { $TargetName = if ($Target -eq 'Editor') { "$($Ctx.Name)Editor" } else { $Ctx.Name } }
$LogDir = Join-Path $Ctx.ProjectRoot 'Saved\Logs'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$Log = Join-Path $LogDir "adsk_build_${Target}_$Configuration.log"

Write-Host "Building $TargetName Win64 $Configuration..."
$Status = (Invoke-AdskLogged $Ctx.BuildBat (@($TargetName, 'Win64', $Configuration, "-Project=$($Ctx.UProject)", '-WaitMutex', '-NoHotReload') + $ExtraArgs) $Log).Status

# MSVC: `file(line): error C1234:`; linker: `x.obj : error LNK2019:`; UBT/UHT: `Error:` lines.
Select-String -LiteralPath $Log -Pattern ':\s+(fatal )?error [A-Z]+\d+|note: see declaration|^\s*Error:|^Result:' |
	Select-Object -First 40 | ForEach-Object { Write-Host $_.Line }
# UBT can exit 0 without building; the Result line is what it actually says.
if ($Status -eq 0 -and (Select-String -LiteralPath $Log -Pattern '^Result: Succeeded' -Quiet)) {
	Write-Host "PASS adsk_build [$TargetName $Configuration]"
	exit 0
}
Write-Host "FAIL adsk_build [$TargetName $Configuration] exit=$Status. Log: $Log"
exit 1
