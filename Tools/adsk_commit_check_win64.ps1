<#
Tools/adsk_commit_check_win64.ps1 - check that commit messages and pull request descriptions say what was verified.
The Win64 twin of Tools/adsk_commit_check.sh, which lists the rules. No engine and no project; Python 3.8+ (python,
py -3, or the engine's bundled interpreter when UE_ROOT is set) and git are needed.

    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_commit_check_win64.ps1 [-Range <a..b>]
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_commit_check_win64.ps1 -Message <file>
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_commit_check_win64.ps1 -PrBody <file>

-Range defaults to origin/main..HEAD and checks the non-merge commits that touch this plugin.

Exit codes: 0 clean, 1 findings, 2 usage, no Python, no git or a git failure.
#>
[CmdletBinding()]
param(
	[string] $Range,
	[string] $Message,
	[string] $PrBody
)

trap { [Console]::Error.WriteLine("ERROR: $($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber): $_"); exit 2 }
. (Join-Path $PSScriptRoot 'lib\adsk_env_win64.ps1')

$Given = @($Range, $Message, $PrBody | Where-Object { $_ })
if ($Given.Count -gt 1) { Write-AdskError 'pass at most one of -Range, -Message, -PrBody.'; exit 2 }
if ($Message) { $Mode = '--message'; $Value = $Message }
elseif ($PrBody) { $Mode = '--pr-body'; $Value = $PrBody }
else { $Mode = '--range'; $Value = $(if ($Range) { $Range } else { 'origin/main..HEAD' }) }

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Write-AdskError 'git is required.'; exit 2 }
$Python = Get-AdskPython $env:UE_ROOT
if (-not $Python) { Write-AdskError 'Python 3.8+ is required (python, py -3, or set UE_ROOT for the engine''s own).'; exit 2 }

$PyArgs = $Python.Args
& $Python.Exe @PyArgs -I (Join-Path $PSScriptRoot 'lib\adsk_commit_check.py') $AdskPluginDir $Mode $Value
exit $LASTEXITCODE
