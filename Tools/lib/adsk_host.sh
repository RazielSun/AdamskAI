# A throwaway C++ host project for the plugin, shared by adsk_host_check.sh and adsk_bench.sh. Sourced after
# adsk_env.sh; never executed.
#
#   adsk_make_host "$HOST" "$ASSOCIATION" '{ "Name": "AdamskAI", "Enabled": true }'
#   adsk_build_host || exit 1
#
# adsk_make_host writes AdskHost.uproject (one empty Runtime module, the given plugin entries), its Game and Editor
# targets, and copies this plugin into Plugins/AdamskAI without Binaries/, Intermediate/ and Saved/, so nothing built
# for the calling project leaks in. It sets ADSK_HOST_NAME and ADSK_UPROJECT. adsk_build_host builds the host editor,
# Development, into $HOST/build.log, and prints the errors when it fails.

ADSK_HOST_NAME="AdskHost"

adsk_make_host() {
	local host="$1" association="$2" plugins="$3" name="$ADSK_HOST_NAME" target class
	mkdir -p "$host/Source/$name" "$host/Plugins" "$host/Config" || return 2
	cat >"$host/$name.uproject" <<EOF
{
	"FileVersion": 3,
	"EngineAssociation": "$association",
	"Modules": [ { "Name": "$name", "Type": "Runtime", "LoadingPhase": "Default" } ],
	"Plugins": [ $plugins ]
}
EOF
	for target in Game Editor; do
		class="$name"; [ "$target" = "Editor" ] && class="${name}Editor"
		cat >"$host/Source/$class.Target.cs" <<EOF
using UnrealBuildTool;

public class ${class}Target : TargetRules
{
	public ${class}Target(TargetInfo Target) : base(Target)
	{
		Type = TargetType.$target;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("$name");
	}
}
EOF
	done
	cat >"$host/Source/$name/$name.Build.cs" <<EOF
using UnrealBuildTool;

public class $name : ModuleRules
{
	public $name(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
EOF
	cat >"$host/Source/$name/$name.cpp" <<EOF
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, $name, "$name");
EOF
	rsync -a --exclude=/Binaries --exclude=/Intermediate --exclude=/Saved "$ADSK_PLUGIN_DIR/" "$host/Plugins/AdamskAI/" \
		|| { adsk_err "copying the plugin failed."; return 2; }
	ADSK_HOST="$host"
	ADSK_UPROJECT="$host/$name.uproject"
}

adsk_build_host() {
	local build="$ADSK_UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" log="$ADSK_HOST/build.log"
	[ -x "$build" ] || { adsk_err "no Linux Build.sh at $build."; return 2; }
	"$build" "${ADSK_HOST_NAME}Editor" Linux Development "-project=$ADSK_UPROJECT" >"$log" 2>&1 \
		&& grep -q "^Result: Succeeded" "$log" && return 0
	grep -E "error:|^Result:" "$log" | head -20
	echo "host build failed. Log: $log"
	return 1
}
