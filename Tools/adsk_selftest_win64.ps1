<#
Tools/adsk_selftest_win64.ps1 - run the Win64 scripts' failure paths and check each exit code. Win64 only for now; the Linux twin is owed with the next Linux run.

    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_selftest_win64.ps1
    powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_selftest_win64.ps1 -SkipEditor

Cases, each a real run of a script against the real engine (a few minutes in all):
  check-clean          adsk_check on this plugin                                   exit 0
  check-side-effect    adsk_check on a copy with check(Mutate()) planted            exit 1
  schema-refused       adsk_schema_check on a project with a file the schema refuses exit 1
  validate-broken      adsk_validate -Paths <a broken .xml>                         exit 1
  validate-missing     adsk_validate -Paths <a relative path to no file>            exit 1, "no such file"
  validate-project     adsk_validate -Project <a directory without a .uproject>     exit 2
  test-no-match        adsk_test <a filter matching nothing>                         exit 1
  test-timeout         adsk_test with ADSK_TEST_TIMEOUT=3                            exit 3
  build-editor-open    adsk_build while an editor for the project runs              exit 2, the editor's PID listed
  strict-editor-open   adsk_strict_includes while an editor for the project runs    exit 2
-SkipEditor leaves out the last two, which start an editor (UnrealEditor-Cmd, -nullrhi) and stop it by its PID.
No editor may be running when this starts. Build the editor target first (Tools\adsk_build_win64.ps1).

Exit codes: 0 every case got its exit code; 1 a case did not; 2 setup (project, engine, a running editor).
#>
[CmdletBinding()]
param(
	[string] $Project,
	[switch] $SkipEditor
)

trap { [Console]::Error.WriteLine("ERROR: $($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber): $_"); exit 2 }
. (Join-Path $PSScriptRoot 'lib\adsk_env_win64.ps1')

$Ctx = Resolve-AdskProject $Project
if (-not $Ctx) { exit 2 }
if (-not (Resolve-AdskEngine $Ctx)) { exit 2 }
if (-not (Assert-AdskNoEditor $Ctx)) { exit 2 }

$Scratch = Join-Path ([IO.Path]::GetTempPath()) ("adsk_selftest_" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $Scratch | Out-Null
$Failed = 0

# Runs a Tools script in a child PowerShell from the project root; returns @{ Code; Output }.
function Invoke-Script([string] $Script, [string[]] $Arguments, [hashtable] $Environment = @{}) {
	$Saved = @{}
	foreach ($Key in $Environment.Keys) { $Saved[$Key] = [Environment]::GetEnvironmentVariable($Key); [Environment]::SetEnvironmentVariable($Key, $Environment[$Key]) }
	Push-Location $Ctx.ProjectRoot
	try {
		$Output = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $Script) @Arguments 2>&1 | ForEach-Object { "$_" }
		$Code = $LASTEXITCODE
	} finally {
		Pop-Location
		foreach ($Key in $Saved.Keys) { [Environment]::SetEnvironmentVariable($Key, $Saved[$Key]) }
	}
	return @{ Code = $Code; Output = ($Output -join "`n") }
}

function Expect([string] $Case, $Run, [int] $Code, [string] $Contains = '') {
	$Ok = $Run.Code -eq $Code -and (-not $Contains -or $Run.Output.Contains($Contains))
	if ($Ok) { Write-Host "PASS $Case (exit $($Run.Code))"; return }
	$script:Failed++
	Write-Host "FAIL $Case`: exit $($Run.Code), expected $Code$(if ($Contains) { " with '$Contains'" })"
	$Run.Output -split "`n" | Select-Object -Last 15 | ForEach-Object { Write-Host "    $_" }
}

try {
	Expect 'check-clean' (Invoke-Script 'adsk_check_win64.ps1' @()) 0

	# A copy of Source and Tools: adsk_check reads the plugin its script sits in.
	$Copy = Join-Path $Scratch 'AdamskAI'
	New-Item -ItemType Directory -Force -Path $Copy | Out-Null
	Copy-Item -Recurse -LiteralPath (Join-Path $Ctx.PluginDir 'Source') -Destination $Copy
	Copy-Item -Recurse -LiteralPath (Join-Path $Ctx.PluginDir 'Tools') -Destination $Copy
	$Planted = Join-Path $Copy 'Source\AdamskAI\Private\SelfTestPlanted.cpp'
	[IO.File]::WriteAllText($Planted, "// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.`n`nstatic int Mutate() { return 1; }`nstatic void Planted() { check(Mutate()); }`n")
	$Run = @{ Code = 0; Output = '' }
	$Output = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Copy 'Tools\adsk_check_win64.ps1') 2>&1 | ForEach-Object { "$_" }
	$Run.Code = $LASTEXITCODE; $Run.Output = $Output -join "`n"
	Expect 'check-side-effect' $Run 1 'check-side-effect'

	# A scratch project around a copy of Tools and docs, whose behavior root holds a file the schema refuses.
	$Host_ = Join-Path $Scratch 'SchemaHost'
	$HostPlugin = Join-Path $Host_ 'Plugins\AdamskAI'
	New-Item -ItemType Directory -Force -Path $HostPlugin, (Join-Path $Host_ 'Content\AI') | Out-Null
	[IO.File]::WriteAllText((Join-Path $Host_ 'SchemaHost.uproject'), '{ "FileVersion": 3 }')
	Copy-Item -Recurse -LiteralPath (Join-Path $Ctx.PluginDir 'Tools') -Destination $HostPlugin
	Copy-Item -Recurse -LiteralPath (Join-Path $Ctx.PluginDir 'docs') -Destination $HostPlugin
	[IO.File]::WriteAllText((Join-Path $Host_ 'Content\AI\refused.xml'), '<Agent version="1" id="refused" selftest="1"/>')
	$Output = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $HostPlugin 'Tools\adsk_schema_check_win64.ps1') -Project $Host_ 2>&1 | ForEach-Object { "$_" }
	Expect 'schema-refused' @{ Code = $LASTEXITCODE; Output = $Output -join "`n" } 1 'FAIL adsk_schema_check'

	$Broken = Join-Path $Scratch 'broken.xml'
	[IO.File]::WriteAllText($Broken, '<Agent version="1" id="broken"><StateMachine')
	Expect 'validate-broken' (Invoke-Script 'adsk_validate_win64.ps1' @('-Paths', $Broken)) 1
	Expect 'validate-missing' (Invoke-Script 'adsk_validate_win64.ps1' @('-Paths', 'Content\AI\no_such_selftest_file.xml')) 1 'no such file'
	Expect 'validate-project' (Invoke-Script 'adsk_validate_win64.ps1' @('-Project', $Scratch)) 2

	Expect 'test-no-match' (Invoke-Script 'adsk_test_win64.ps1' @('AdamskAI.NoSuchSelfTestFilter.')) 1
	Expect 'test-timeout' (Invoke-Script 'adsk_test_win64.ps1' @('AdamskAI.') @{ ADSK_TEST_TIMEOUT = '3' }) 3

	if (-not $SkipEditor) {
		# Any long-lived editor process on this project's command line counts; -nullrhi keeps it windowless.
		$Editor = Start-Process -FilePath $Ctx.EditorCmd -ArgumentList "`"$($Ctx.UProject)`" -nullrhi -unattended -nosplash -nosound" -PassThru -WindowStyle Hidden
		try {
			$Deadline = (Get-Date).AddSeconds(60)
			while (-not (Get-AdskRunningEditors $Ctx | Where-Object { $_.ProcessId -eq $Editor.Id }) -and (Get-Date) -lt $Deadline) { Start-Sleep -Milliseconds 500 }
			Expect 'build-editor-open' (Invoke-Script 'adsk_build_win64.ps1' @()) 2 "$($Editor.Id)"
			Expect 'strict-editor-open' (Invoke-Script 'adsk_strict_includes_win64.ps1' @()) 2 "$($Editor.Id)"
		} finally {
			# Our own process, by its PID; never by name.
			& taskkill.exe /PID $Editor.Id /T /F 2>&1 | Out-Null
			$null = $Editor.WaitForExit(30000)
		}
	}
} finally {
	Remove-Item -Recurse -Force -LiteralPath $Scratch -ErrorAction SilentlyContinue
}

Write-Host "selftest: $Failed case(s) failed"
exit $(if ($Failed -eq 0) { 0 } else { 1 })
