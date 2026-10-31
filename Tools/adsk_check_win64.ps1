<#
Tools/adsk_check_win64.ps1 - static rules for the AdamskAI plugin's own source. The Win64 twin of
Tools/adsk_check.sh. No engine build and no project needed; Python 3.8+ is (python, py -3, or the
engine's bundled interpreter when UE_ROOT is set).

    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_check_win64.ps1

ONE RULE FILE FOR BOTH PLATFORMS. The rules live in Tools\lib\adsk_check.py and both twins run it, so
they cannot drift; adsk_check.sh's header lists them. Findings print as `path:line: rule: message`.

Exit codes: 0 clean, 1 findings, 2 setup failure (no Python, plugin layout not found).
#>
[CmdletBinding()]
param()

# Any script error is a tooling failure (exit 2), never a silent pass.
trap { [Console]::Error.WriteLine("ERROR: $($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber): $_"); exit 2 }
. (Join-Path $PSScriptRoot 'lib\adsk_env_win64.ps1')

if (-not (Test-Path -LiteralPath (Join-Path $AdskPluginDir 'Source') -PathType Container)) {
	Write-AdskError "no Source\ under $AdskPluginDir."; exit 2
}
$Python = Get-AdskPython $env:UE_ROOT
if (-not $Python) { Write-AdskError 'Python 3.8+ is required (python, py -3, or set UE_ROOT for the engine''s own).'; exit 2 }

$PyArgs = $Python.Args
& $Python.Exe @PyArgs -I (Join-Path $PSScriptRoot 'lib\adsk_check.py') $AdskPluginDir
exit $LASTEXITCODE
