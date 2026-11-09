# Static plugin rules shared by Tools/adsk_check.sh and Tools/adsk_check_win64.ps1; see adsk_check.sh for the
# rule list. Usage: python3 -I adsk_check.py <plugin-dir>. Exit codes: 0 clean, 1 findings.

import re
import sys
from pathlib import Path

plugin = Path(sys.argv[1])
source = plugin / "Source"
HEADER = "// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE."
ALLOWED_DEPENDENCIES = {
    "Core", "CoreUObject", "Engine", "AIModule", "DeveloperSettings", "Projects", "Json", "Expat",
    "DirectoryWatcher", "AssetRegistry", "CQTest", "GameplayDebugger", "GameplayTags",
    "AdamskAI", "AdamskAIHost", "AdamskAIDebug", "AdamskAIDemo", "AdamskAIEditor",
    # Only the tests' world specs and their map generator (editor block) use these.
    "NavigationSystem", "GameplayTasks", "UnrealEd",
    # The core's Insights channel (private), and the tests that toggle it.
    "TraceLog",
    # CQTest's PIE network component replicates through Iris in its header templates (tests, editor block).
    "IrisCore",
}
# check() compiles out of Shipping and Test, so its argument may only ask questions. An allowlist: harmless
# names (ResolveEnumValue, Render) still fill out-parameters. Get* passes only with no arguments.
CHECK_ARGUMENT = re.compile(r"\bcheck(?:Slow)?\s*\((.*)")
CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\((\s*\))?")
PURE_CALL = re.compile(r"^(?:Is|Has|Can|Find|Contains|Equals|Num|Len|Matches|StartsWith|EndsWith|TEXT$)\w*$")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
DEPENDENCY_BLOCK = re.compile(r"DependencyModuleNames\.(?:AddRange|Add)\s*\((.*?)\)\s*;", re.S)
QUOTED = re.compile(r'"([^"]+)"')

findings = []


def report(path, line, rule, message):
    findings.append(f"{path.relative_to(plugin)}:{line}: {rule}: {message}")


files = sorted(p for p in source.rglob("*") if p.suffix in (".h", ".cpp", ".cs") and p.is_file())
for path in files:
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    module = path.relative_to(source).parts[0]

    if not lines or lines[0] != HEADER:
        report(path, 1, "license-header", "first line must be the MIT header")

    if path.suffix == ".cs":
        for block in DEPENDENCY_BLOCK.findall(text):
            for name in QUOTED.findall(block):
                if name not in ALLOWED_DEPENDENCIES:
                    report(path, 1, "module-dependency", f"'{name}' is not an allowed plugin dependency")
                if module == "AdamskAI" and name == "Engine":
                    report(path, 1, "core-engine-free", "the core module must not depend on Engine")
        continue

    includes = [(number, match.group(1)) for number, line in enumerate(lines, 1) if (match := INCLUDE.match(line))]
    if path.suffix == ".h":
        generated = [number for number, name in includes if name.endswith(".generated.h")]
        if generated and generated[-1] != includes[-1][0]:
            report(path, generated[-1], "generated-last", "the .generated.h include must be the last #include")

    for number, name in includes:
        if module == "AdamskAI" and name.startswith(("Engine/", "GameFramework/", "Components/")):
            report(path, number, "core-engine-free", f"the core module must not include '{name}'")
        if "imgui" in name.lower():
            report(path, number, "no-imgui", f"the plugin draws no ImGui; '{name}' belongs to a project's own panel")

    for number, line in enumerate(lines, 1):
        code = line.split("//", 1)[0]
        argument = CHECK_ARGUMENT.search(code)
        if not argument:
            continue
        for call in CALL.finditer(argument.group(1)):
            name, no_arguments = call.group(1), call.group(2)
            if PURE_CALL.match(name) or (name.startswith("Get") and no_arguments):
                continue
            report(path, number, "check-side-effect", f"'{name}(...)' compiles out with check(); store the result or use verify()")
            break

# docs/semantics.md: every rule of the ordered sections names what checks it, and every name resolves.
semantics = plugin / "docs" / "semantics.md"
conformance = {p.stem for p in (plugin / "Fixtures" / "conformance").glob("*.xml")}
test_names = set(re.findall(r'TEST_CLASS\w*\(\s*\w+\s*,\s*"([^"]+)"', "\n".join(
    p.read_text(encoding="utf-8", errors="replace") for p in (source / "AdamskAITests").rglob("*.cpp"))))
CITATION = re.compile(r"\[([^\]]+)\](?!\()")
RULE = re.compile(r"^(?:\d+\.|-)\s+")
CHECKED_SECTIONS = ("Start", "Tick(Δ)", "When an event becomes eligible", "Budgets")
if semantics.exists():
    # Blocks: a list item with its continuation lines, a table row, or (under Budgets) a paragraph.
    blocks, section, current = [], None, None
    for number, line in enumerate(semantics.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if line.startswith("## "):
            section, current = line[3:].strip(), None
            continue
        if section not in CHECKED_SECTIONS or not stripped:
            current = None if section == "Budgets" else current
            continue
        if stripped.startswith("|"):
            current = None
            if not stripped.startswith("|---") and "Raised by" not in stripped:
                blocks.append([number, stripped])
        elif RULE.match(stripped) or (section == "Budgets" and current is None):
            current = [number, stripped]
            blocks.append(current)
        elif current is not None:
            current[1] += " " + stripped
    for number, text in blocks:
        names = [part.strip() for group in CITATION.findall(text) for part in group.split(",")]
        if not names and not text.endswith(":"):
            report(semantics, number, "semantics-citation", "the rule names no conformance file or spec that checks it")
        for name in names:
            if name.startswith("AdamskAI"):
                if not any(test == name or test.startswith(name + ".") for test in test_names):
                    report(semantics, number, "semantics-citation", f"no test registers '{name}'")
            elif name not in conformance:
                report(semantics, number, "semantics-citation", f"no Fixtures/conformance/{name}.xml")


def test_registered(name):
    # A test class, a prefix of one, or one of its methods.
    return any(test == name or test.startswith(name + ".") or name.startswith(test + ".") for test in test_names)


# docs/dev/decisions: numbered files with a known status and evidence that still resolves.
decisions = plugin / "docs" / "dev" / "decisions"
DECISION_NAME = re.compile(r"^(\d{4})-[a-z0-9]+(?:-[a-z0-9]+)*\.md$")
STATUS = re.compile(r"^(?:Proposed|Accepted, \d{4}-\d{2}-\d{2}|Superseded by (\d{4}))$")
if decisions.is_dir():
    numbers = {}
    for path in sorted(decisions.glob("*.md")):
        if path.name == "README.md":
            continue
        name = DECISION_NAME.match(path.name)
        if not name:
            report(path, 1, "decision-evidence", "a decision file is named NNNN-lowercase-slug.md")
            continue
        number = name.group(1)
        if number in numbers:
            report(path, 1, "decision-evidence", f"number {number} is also {numbers[number]}")
        numbers[number] = path.name
        lines = path.read_text(encoding="utf-8").splitlines()
        if not lines or not lines[0].startswith(f"# {number}: "):
            report(path, 1, "decision-evidence", f"the first line is '# {number}: <title>'")
        fields = {}
        for line_number, line in enumerate(lines, 1):
            if line.startswith("## "):
                break
            key, _, value = line.partition(": ")
            if key in ("Status", "Evidence"):
                fields[key] = (line_number, value.strip())
        if "Status" not in fields:
            report(path, 1, "decision-evidence", "no 'Status:' line before the first section")
        elif not (status := STATUS.match(fields["Status"][1])):
            report(path, fields["Status"][0], "decision-evidence",
                   "Status is 'Proposed', 'Accepted, YYYY-MM-DD' or 'Superseded by NNNN'")
        elif status.group(1) and not list(decisions.glob(f"{status.group(1)}-*.md")):
            report(path, fields["Status"][0], "decision-evidence", f"no decision {status.group(1)} supersedes it")
        if "Evidence" not in fields:
            report(path, 1, "decision-evidence", "no 'Evidence:' line before the first section")
            continue
        line_number, value = fields["Evidence"]
        names = [part.strip() for group in CITATION.findall(value) for part in group.split(",")]
        if not names:
            report(path, line_number, "decision-evidence", "Evidence names no test, fixture or path")
        for cited in names:
            if cited.startswith("AdamskAI"):
                if not test_registered(cited):
                    report(path, line_number, "decision-evidence", f"no test registers '{cited}'")
            elif not (plugin / cited.split("#", 1)[0]).exists() and cited not in conformance:
                report(path, line_number, "decision-evidence", f"no file '{cited}' in the plugin")

for finding in findings:
    print(finding)
print(f"{'FAIL' if findings else 'PASS'} adsk_check: {len(files)} files, {len(findings)} findings")
sys.exit(1 if findings else 0)
