# A clean copy of the plugin for a game project, shared by Tools/adsk_export.sh. The files come from a commit (git
# archive), never from the work tree, and pass the same filter RunUAT BuildPlugin applies: its default set, then
# Config/FilterPlugin.ini, the last matching rule winning. Nothing is compiled; the copy is source.
# Usage: python3 -I adsk_export.py <plugin-dir> <rev> <out-dir>. Exit codes: 0 written, 2 usage, git or write failure.

import io, os, re, shutil, subprocess, sys, tarfile, tempfile

# BuildPluginCommand.Automation.cs, FilterPluginFiles: the .uplugin and these, then /Tests/ out.
DEFAULT_RULES = [("+", "/Resources/..."), ("+", "/Content/..."), ("+", "/Shaders/..."), ("+", "/Source/..."),
                 ("-", "/Tests/...")]


def fail(message):
    print(f"ERROR: {message}", file=sys.stderr)
    sys.exit(2)


def git(plugin, *args, binary=False):
    run = subprocess.run(["git", "-C", plugin, *args], capture_output=True)
    if run.returncode != 0:
        fail(f"git {' '.join(args)}: {run.stderr.decode(errors='replace').strip()}")
    return run.stdout if binary else run.stdout.decode().strip()


def pattern(rule):
    """A FilterPlugin rule as a regex over '/relative/path': '...' any depth, '*' and '?' within a name."""
    anchored = rule.startswith("/")
    body = re.escape(rule.lstrip("/")).replace(r"\.\.\.", ".*").replace(r"\*", "[^/]*").replace(r"\?", "[^/]")
    return re.compile(("^/" if anchored else "^(?:.*/)?") + body + "$")


def rules(ini_text):
    out, section = [], None
    for line in ini_text.splitlines():
        line = line.strip()
        if not line or line.startswith(";"):
            continue
        if line.startswith("["):
            section = line
            continue
        if section == "[FilterPlugin]":
            out.append(("-", line[1:]) if line.startswith("-") else ("+", line))
    return out


def main(argv):
    if len(argv) != 3:
        fail("usage: adsk_export.py <plugin-dir> <rev> <out-dir>")
    plugin, rev, out = os.path.abspath(argv[0]), argv[1], os.path.abspath(argv[2])
    uplugins = [f for f in os.listdir(plugin) if f.endswith(".uplugin")]
    if len(uplugins) != 1:
        fail(f"{plugin} does not hold exactly one .uplugin")
    if os.path.exists(out) and os.listdir(out):
        fail(f"{out} exists and is not empty; pass a new directory")

    commit = git(plugin, "rev-parse", "--verify", f"{rev}^{{commit}}")
    prefix = git(plugin, "rev-parse", "--show-prefix").rstrip("/")
    # From the top level: run in a subdirectory, git archive also limits itself to that subdirectory.
    top = git(plugin, "rev-parse", "--show-toplevel")
    archive = git(top, "archive", "--format=tar", f"{commit}:{prefix}" if prefix else commit, binary=True)
    dirty = git(plugin, "status", "--porcelain", "--", ".")

    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        members = [m for m in tar.getmembers() if m.isfile()]
        names = {m.name for m in members}
        ini = "Config/FilterPlugin.ini"
        if ini not in names:
            fail(f"{ini} is missing at {commit[:7]}")
        all_rules = [("+", "/" + uplugins[0])] + DEFAULT_RULES + rules(tar.extractfile(ini).read().decode())
        compiled = [(sign, pattern(rule)) for sign, rule in all_rules]

        def keep(name):
            decision = False
            for sign, rx in compiled:
                if rx.match("/" + name):
                    decision = sign == "+"
            return decision

        kept = [m for m in members if keep(m.name)]
        parent = os.path.dirname(out) or "."
        os.makedirs(parent, exist_ok=True)
        stage = tempfile.mkdtemp(prefix=".adsk_export.", dir=parent)
        try:
            for m in kept:
                dest = os.path.join(stage, m.name)
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with open(dest, "wb") as f:
                    f.write(tar.extractfile(m).read())
                os.chmod(dest, m.mode & 0o777)
            if not os.path.isfile(os.path.join(stage, uplugins[0])):
                fail("the export has no .uplugin")
            if os.path.exists(out):
                os.rmdir(out)
            os.rename(stage, out)
        except BaseException:
            shutil.rmtree(stage, ignore_errors=True)
            raise

    size = sum(m.size for m in kept)
    tops = sorted({m.name.split("/")[0] for m in kept})
    print(f"wrote {out}: {len(kept)} of {len(members)} files from {commit[:7]}, {size / 1e6:.1f} MB")
    print(f"  top level: {', '.join(tops)}")
    if dirty:
        print(f"  note: {len(dirty.splitlines())} uncommitted change(s) in the plugin are not in it; commit them first")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
