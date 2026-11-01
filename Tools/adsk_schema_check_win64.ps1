<#
Tools/adsk_schema_check_win64.ps1 - check that the generated XML Schemas still accept every valid behavior file. The
language part of adamskai.xsd is written by hand in ADSKSchemaXsd.cpp, so a new attribute the compiler takes but the
schema lacks would underline correct files in the IDE; this catches it. No engine needed. The Win64 twin of
Tools/adsk_schema_check.sh.

    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_schema_check_win64.ps1

Checks docs\adamskai.xsd against Examples, Demo, Fixtures\valid and Fixtures\conformance, and the project's schema
(Content\<BehaviorRoot>\adamskai.xsd, else the plugin's) against the behavior root and Fixtures\world. Run
adsk_validate first: it checks that both schemas are current. Uses .NET's XmlSchemaSet.

Exit codes: 0 every file passes; 1 a file a schema refuses; 2 usage, project or a schema that does not load.
#>
[CmdletBinding()]
param([string] $Project)

trap { [Console]::Error.WriteLine("ERROR: $($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber): $_"); exit 2 }
. (Join-Path $PSScriptRoot 'lib\adsk_env_win64.ps1')

$Ctx = Resolve-AdskProject $Project
if (-not $Ctx) { exit 2 }

$Root = 'AI'
$Ini = Join-Path $Ctx.ProjectRoot 'Config\DefaultGame.ini'
if (Test-Path -LiteralPath $Ini) {
	foreach ($Line in Get-Content -LiteralPath $Ini) { if ($Line -match '^\s*BehaviorRoot\s*=\s*"?([^"]+?)"?\s*$') { $Root = $Matches[1] } }
}
$BehaviorRoot = Join-Path $Ctx.ProjectRoot "Content\$Root"
$PluginSchema = Join-Path $Ctx.PluginDir 'docs\adamskai.xsd'
$ProjectSchema = Join-Path $BehaviorRoot 'adamskai.xsd'
if (-not (Test-Path -LiteralPath $ProjectSchema)) { $ProjectSchema = $PluginSchema }

$script:Problems = 0
$script:Files = 0

function Test-Schema([string] $Xsd, [string[]] $Dirs) {
	$Set = New-Object System.Xml.Schema.XmlSchemaSet
	$Set.XmlResolver = $null
	try { [void] $Set.Add($null, $Xsd); $Set.Compile() }
	catch { Write-AdskError "schema ${Xsd} does not load: $($_.Exception.Message)"; exit 2 }
	foreach ($Dir in $Dirs) {
		if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { continue }
		foreach ($File in Get-ChildItem -LiteralPath $Dir -Filter *.xml -Recurse) {
			$script:Files++
			$Settings = New-Object System.Xml.XmlReaderSettings
			$Settings.ValidationType = [System.Xml.ValidationType]::Schema
			$Settings.Schemas = $Set
			$Path = $File.FullName
			# A closure runs in its own module, where $script: is another variable: the handler only fills a list it
			# captured, and the count is taken from the list after the read.
			$Errors = New-Object System.Collections.Generic.List[string]
			$Settings.add_ValidationEventHandler({ param($Sender, $Event)
				$Errors.Add("${Path}:$($Event.Exception.LineNumber): error schema: $($Event.Message)") }.GetNewClosure())
			$Reader = [System.Xml.XmlReader]::Create($Path, $Settings)
			try { while ($Reader.Read()) {} }
			catch { $Errors.Add("${Path}: error schema: $($_.Exception.InnerException.Message)") }
			finally { $Reader.Close() }
			foreach ($Line in $Errors) { Write-Host $Line }
			$script:Problems += $Errors.Count
		}
	}
}

Test-Schema $PluginSchema @((Join-Path $Ctx.PluginDir 'Examples'), (Join-Path $Ctx.PluginDir 'Demo'), (Join-Path $Ctx.PluginDir 'Fixtures\valid'), (Join-Path $Ctx.PluginDir 'Fixtures\conformance'))
Test-Schema $ProjectSchema @($BehaviorRoot, (Join-Path $Ctx.PluginDir 'Fixtures\world'))

if ($script:Problems -eq 0) { Write-Host "PASS adsk_schema_check: $($script:Files) files"; exit 0 }
Write-Host "FAIL adsk_schema_check: $($script:Problems) problem(s) in $($script:Files) files"
exit 1
