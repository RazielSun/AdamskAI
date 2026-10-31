# Tools/lib/adsk_env_win64.ps1 - project, engine and process helpers shared by Tools\adsk_*_win64.ps1.
# The Win64 twin of Tools/lib/adsk_env.sh. Dot-sourced, never executed. Windows PowerShell 5.1 and
# PowerShell 7 both work.
#
#   . (Join-Path $PSScriptRoot 'lib\adsk_env_win64.ps1')
#   $Ctx = Resolve-AdskProject $Project;  if (-not $Ctx) { exit 2 }
#   if (-not (Resolve-AdskEngine $Ctx)) { exit 2 }
#
# The context carries PluginDir, UProject, ProjectRoot, Name, then UERoot, EditorCmd, BuildBat and
# RunUAT. Every failure writes `ERROR: ...` and the caller exits 2, as the bash scripts do.
#
# PROJECT: the explicit -Project, else the Git worktree root, else the nearest ancestor of the plugin,
# each holding exactly one .uproject. ENGINE: UE_ROOT, else the .uproject EngineAssociation looked up
# in HKCU:\Software\Epic Games\Unreal Engine\Builds (source builds), then
# HKLM:\SOFTWARE\EpicGames\Unreal Engine\<association> (launcher installs). The registry keeps entries
# for engines whose folders were deleted, so the editor executable itself is what proves an engine.
#
# WINDOWS POWERSHELL 5.1 TRAPS every helper here is written around:
#   - a function that returns a one-element or empty array returns the element or $null; callers wrap
#     results they count in @(...);
#   - `*>` writes UTF-16 and turns a native tool's stderr into error records, so native output goes
#     through `cmd /c ... >log 2>&1` (Invoke-AdskLogged);
#   - $PSScriptRoot is empty inside param() defaults; scripts derive paths after param();
#   - a statement error does not stop a script by itself; every script installs a trap that exits 2.

Set-StrictMode -Version 2.0

$script:AdskPluginDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

function Write-AdskError([string] $Message) { [Console]::Error.WriteLine("ERROR: $Message") }

# The only .uproject directly inside a directory; $null when there are none or several.
function Get-AdskSingleUProject([string] $Dir) {
	$Found = @(Get-ChildItem -LiteralPath $Dir -Filter '*.uproject' -File -ErrorAction SilentlyContinue)
	if ($Found.Count -eq 1) { return $Found[0].FullName }
	return $null
}

function Resolve-AdskProject([string] $Explicit) {
	$UProject = $null
	if ($Explicit) {
		if ((Test-Path -LiteralPath $Explicit -PathType Leaf) -and $Explicit.EndsWith('.uproject')) {
			$UProject = (Resolve-Path -LiteralPath $Explicit).Path
		} elseif (Test-Path -LiteralPath $Explicit -PathType Container) {
			$UProject = Get-AdskSingleUProject $Explicit
			if (-not $UProject) { Write-AdskError "-Project '$Explicit' is a directory without exactly one .uproject."; return $null }
		} else {
			Write-AdskError "-Project '$Explicit' is neither a .uproject file nor a directory."; return $null
		}
	} else {
		$Root = $null
		if (Get-Command git -ErrorAction SilentlyContinue) {
			$Root = & git -C $script:AdskPluginDir rev-parse --show-toplevel 2>$null
			if ($LASTEXITCODE -ne 0) { $Root = $null }
		}
		if ($Root) {
			$UProject = Get-AdskSingleUProject $Root
			if (-not $UProject) { Write-AdskError "the Git root '$Root' does not hold exactly one .uproject; pass -Project."; return $null }
		} else {
			$Dir = Split-Path $script:AdskPluginDir -Parent
			while ($Dir) {
				$UProject = Get-AdskSingleUProject $Dir
				if ($UProject) { break }
				$Dir = Split-Path $Dir -Parent
			}
			if (-not $UProject) { Write-AdskError 'no project found above the plugin; pass -Project.'; return $null }
		}
	}
	return [pscustomobject]@{
		PluginDir   = $script:AdskPluginDir
		UProject    = $UProject
		ProjectRoot = Split-Path $UProject -Parent
		Name        = [IO.Path]::GetFileNameWithoutExtension($UProject)
		UERoot      = $null
		EditorCmd   = $null
		BuildBat    = $null
		RunUAT      = $null
	}
}

# Engine root for an association, or $null. UE_ROOT always wins.
function Get-AdskEngineRoot([string] $Association) {
	if ($env:UE_ROOT) { return $env:UE_ROOT.TrimEnd('\', '/') }
	if (-not $Association) { return $null }
	$Builds = Get-ItemProperty 'HKCU:\Software\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
	if ($Builds -and $Builds.PSObject.Properties[$Association]) { return $Builds.$Association.TrimEnd('\', '/') }
	$Installed = Get-ItemProperty "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Association" -ErrorAction SilentlyContinue
	if ($Installed -and $Installed.PSObject.Properties['InstalledDirectory']) { return $Installed.InstalledDirectory.TrimEnd('\', '/') }
	return $null
}

function Resolve-AdskEngine($Ctx) {
	$Association = $null
	try { $Association = (Get-Content -LiteralPath $Ctx.UProject -Raw | ConvertFrom-Json).EngineAssociation } catch { }
	$Root = Get-AdskEngineRoot $Association
	$EditorCmd = if ($Root) { Join-Path $Root 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' } else { $null }
	if (-not $Root -or -not (Test-Path -LiteralPath $EditorCmd -PathType Leaf)) {
		# The registry can keep entries for engines whose folders were deleted; say which one was tried.
		$Tried = if ($Root) { " (tried '$Root')" } else { '' }
		Write-AdskError "no engine for association '$Association'$Tried. Set UE_ROOT to an engine root containing"
		[Console]::Error.WriteLine("       Engine\Binaries\Win64\UnrealEditor-Cmd.exe, or register the engine with the launcher.")
		return $false
	}
	$Ctx.UERoot = $Root
	$Ctx.EditorCmd = $EditorCmd
	$Ctx.BuildBat = Join-Path $Root 'Engine\Build\BatchFiles\Build.bat'
	$Ctx.RunUAT = Join-Path $Root 'Engine\Build\BatchFiles\RunUAT.bat'
	return $true
}

# A Python 3.8+ interpreter as @{ Exe; Args }, or $null. Order: python, py -3, the engine's bundled
# interpreter when an engine root is known. Run it with `& $Py.Exe @($Py.Args) <script> ...`.
function Get-AdskPython([string] $UERoot) {
	$Candidates = @(@{ Exe = 'python'; Args = @() }, @{ Exe = 'py'; Args = @('-3') })
	if ($UERoot) { $Candidates += @{ Exe = (Join-Path $UERoot 'Engine\Binaries\ThirdParty\Python3\Win64\python.exe'); Args = @() } }
	foreach ($Candidate in $Candidates) {
		if (-not (Get-Command $Candidate.Exe -ErrorAction SilentlyContinue)) { continue }
		# The Microsoft Store alias for python exists on PATH but fails; actually running it is the test.
		$PyArgs = $Candidate.Args
		& $Candidate.Exe @PyArgs -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)' 2>$null | Out-Null
		if ($LASTEXITCODE -eq 0) { return [pscustomobject] $Candidate }
	}
	return $null
}

# Optional host-project preflight, run before any editor launch or build. ADSK_PREFLIGHT names a
# command that exits nonzero to refuse the run; ADSK_PREFLIGHT=none disables it. Without the variable,
# a project that ships Tools\lib\preflight_win64.ps1 (the twin of its Tools/lib/preflight.sh) has it
# run in a child PowerShell. Returns the exit code to stop with, or 0 to continue.
function Invoke-AdskPreflight($Ctx) {
	if ($env:ADSK_PREFLIGHT) {
		if ($env:ADSK_PREFLIGHT -eq 'none') { return 0 }
		if (-not (Get-Command $env:ADSK_PREFLIGHT -ErrorAction SilentlyContinue)) {
			Write-AdskError "ADSK_PREFLIGHT '$($env:ADSK_PREFLIGHT)' is not a runnable command."; return 2
		}
		& $env:ADSK_PREFLIGHT
		return $LASTEXITCODE
	}
	$Lib = Join-Path $Ctx.ProjectRoot 'Tools\lib\preflight_win64.ps1'
	if (-not (Test-Path -LiteralPath $Lib -PathType Leaf)) { return 0 }
	& powershell -NoProfile -ExecutionPolicy Bypass -File $Lib
	return $LASTEXITCODE
}

# Editor processes whose command line names this project (or no project at all, which can be any).
function Get-AdskRunningEditors($Ctx) {
	$Name = [regex]::Escape($Ctx.Name + '.uproject')
	@(Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction SilentlyContinue |
		Where-Object { $_.CommandLine -match $Name -or $_.CommandLine -notmatch '\.uproject' })
}

# NO EDITOR MAY BE RUNNING while a build links this project's modules: it holds the DLLs open and the
# link fails on a file it cannot replace (Live Coding owns them). Report the PIDs and refuse; never
# kill, and never by a name pattern, which can match the caller's own shell. Returns $true when clear.
function Assert-AdskNoEditor($Ctx) {
	$Running = @(Get-AdskRunningEditors $Ctx)
	if ($Running.Count -eq 0) { return $true }
	Write-AdskError 'an editor for this project is running and holds the module DLLs; close it first. PIDs:'
	$Running | ForEach-Object { [Console]::Error.WriteLine("       $($_.ProcessId) $($_.Name)") }
	return $false
}

# One argument in the form UnrealEditor parses from its raw Windows command line: `-Key=Value` with a
# space becomes `-Key="Value"` (FParse::Value stops at an unquoted space), anything else with a space
# is quoted whole. The bash scripts get the same result from FCommandLine::BuildFromArgV. cmd.exe
# metacharacters are quoted too, since the line runs through `cmd /c`.
function ConvertTo-AdskArgument([string] $Arg) {
	if ($Arg -notmatch '[\s"&|<>^()%!]') { return $Arg }
	if ($Arg -match '["%!]') { throw "argument must not contain a double quote, % or !: $Arg" }
	if ($Arg -match '^(-[^=\s]+=)(.*)$') { return $Matches[1] + '"' + $Matches[2].TrimEnd('\') + '"' }
	return '"' + $Arg.TrimEnd('\') + '"'
}

# Runs a program with stdout and stderr merged into $LogPath as raw bytes (as `>log 2>&1` in the bash
# scripts; PowerShell's own `*>` would write UTF-16 and turn stderr lines into error records).
# Returns @{ Status = <exit code>; TimedOut = <bool> }. With a positive timeout the whole process tree
# is killed when it expires and Status is 124, the code GNU timeout uses, so callers keep the bash
# scripts' rules. cmd.exe owns the redirection: editor children (crash reporter, shader workers) can
# hold the handles a moment after the editor exits, which makes a PowerShell-side merge race them.
function Invoke-AdskLogged([string] $Exe, [string[]] $Arguments, [string] $LogPath, [int] $TimeoutSeconds = 0) {
	# The same children can still hold the previous run's log when a script runs right after another (adsk_all):
	# cmd then cannot redirect, the editor never starts and the run reads as incomplete. Wait up to 30 s for it.
	$Deadline = (Get-Date).AddSeconds(30)
	while ($true) {
		try { $Stream = [IO.File]::Open($LogPath, 'OpenOrCreate', 'ReadWrite', 'None'); $Stream.Close(); break }
		catch { if ((Get-Date) -gt $Deadline) { break }; Start-Sleep -Milliseconds 250 }
	}
	$ArgLine = "`"$Exe`" " + ((@($Arguments) | ForEach-Object { ConvertTo-AdskArgument $_ }) -join ' ')
	$Process = Start-Process -FilePath $env:ComSpec -ArgumentList "/d /s /c `"$ArgLine >`"$LogPath`" 2>&1`"" -NoNewWindow -PassThru
	# Windows PowerShell only reports ExitCode for a Start-Process object whose handle was opened early.
	$null = $Process.Handle
	if ($TimeoutSeconds -gt 0 -and -not $Process.WaitForExit($TimeoutSeconds * 1000)) {
		& taskkill.exe /PID $Process.Id /T /F 2>&1 | Out-Null
		$null = $Process.WaitForExit(30000)
		return @{ Status = 124; TimedOut = $true }
	}
	$Process.WaitForExit()
	return @{ Status = $Process.ExitCode; TimedOut = $false }
}

# UnrealEditor-Cmd on the context's project with a hard timeout; see Invoke-AdskLogged.
function Invoke-AdskEditor($Ctx, [string[]] $Arguments, [string] $LogPath, [int] $TimeoutSeconds) {
	return Invoke-AdskLogged $Ctx.EditorCmd (@($Ctx.UProject) + $Arguments) $LogPath $TimeoutSeconds
}
