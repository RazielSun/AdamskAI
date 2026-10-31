<#
Tools/adsk_graph_win64.ps1 - the state machine of a behavior file as a diagram. The Win64 twin of
Tools/adsk_graph.sh; both run Tools\lib\adsk_graph.py. No engine build and no project needed; Python 3.8+ is
(python, py -3, or the engine's bundled interpreter when UE_ROOT is set).

    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_graph_win64.ps1 Content\AI\guard.xml [mermaid|dot]

Mermaid (default) renders in GitHub Markdown inside a ```mermaid block; DOT renders with Graphviz (`dot -Tsvg`).

Exit codes: 0 written, 2 usage, no Python, or a file that is not an Agent.
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true, Position = 0)][string]$Path,
	[Parameter(Position = 1)][ValidateSet('mermaid', 'dot')][string]$Format = 'mermaid'
)

# Any script error is a tooling failure (exit 2), never a silent pass.
trap { [Console]::Error.WriteLine("ERROR: $($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber): $_"); exit 2 }
. (Join-Path $PSScriptRoot 'lib\adsk_env_win64.ps1')

$Python = Get-AdskPython $env:UE_ROOT
if (-not $Python) { Write-AdskError 'Python 3.8+ is required (python, py -3, or set UE_ROOT for the engine''s own).'; exit 2 }

$PyArgs = $Python.Args
& $Python.Exe @PyArgs -I (Join-Path $PSScriptRoot 'lib\adsk_graph.py') $Path $Format
exit $LASTEXITCODE
