"""AdamskAI tool cards for the README and docs: validator, simulator, benchmark, demo frame; the statechart card is
statechart.py. Real tool output, kept beside this file (see README.md here). Writes docs/images/*.
Run: python3 -I docs/dev/brand/cards/cards.py [--demo-shot <png from adsk.Demo.Shot>] [--hero-shot <game capture> [<crop w:h:x:y>]]"""
import html, json, os, re, shutil, subprocess, sys, tempfile

SRC = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(SRC, "..", "..", "..", ".."))
OUT = os.path.join(ROOT, "docs", "images")
PROJECT = ROOT
BRAND = os.path.dirname(SRC)
sys.path.insert(0, BRAND)
import build as brand  # noqa: E402

INK, GREEN, AMBER, RED, DIM = brand.INK, brand.GREEN, brand.AMBER, "#ff6b5e", "#8fa198"
GF = ("https://fonts.googleapis.com/css2?family=Teko:wght@500;600&family=JetBrains+Mono:wght@400;700"
      "&family=Noto+Sans:wght@400;600&display=block")

CSS = f"""*{{box-sizing:border-box}} body{{margin:0;position:relative;overflow:hidden;background:{brand.BG1};color:{INK};
font-family:'Noto Sans',sans-serif}}
.wrap{{position:absolute;inset:0;padding:48px 64px}}
.head{{display:flex;align-items:center;gap:18px;margin-bottom:26px}} .head svg{{width:46px;height:46px}}
.kick{{font:400 15px 'JetBrains Mono';letter-spacing:.22em;color:{GREEN};text-transform:uppercase}}
.title{{font:600 50px/1 'Teko';letter-spacing:.01em}} .title em{{font-style:normal;color:{AMBER}}}
.panel{{background:rgba(5,12,9,.86);border:1px solid rgba(62,224,138,.22);border-radius:10px;overflow:hidden}}
.bar{{display:flex;align-items:center;gap:8px;padding:10px 14px;border-bottom:1px solid rgba(62,224,138,.14);
font:400 13px 'JetBrains Mono';color:{DIM}}} .bar i{{width:11px;height:11px;border-radius:50%;background:#2a3a33;display:inline-block}}
pre{{margin:0;padding:18px 22px;font:400 19px/1.55 'JetBrains Mono';white-space:pre;color:#cfe2d6}}
.p{{color:{GREEN}}} .c{{color:{DIM}}} .e{{color:{RED};font-weight:700}} .w{{color:{AMBER}}} .ok{{color:{GREEN};font-weight:700}}
.t{{color:{GREEN}}} .a{{color:#9fc7b3}} .v{{color:{AMBER}}} .cm{{color:#5f7a6c}} .ln{{color:#3f564b;display:inline-block;width:2.4em}}
.bad{{text-decoration:underline wavy {RED};text-underline-offset:5px}}
.foot{{position:absolute;left:64px;right:64px;bottom:26px;font:400 13px 'JetBrains Mono';color:#5f7a6c;letter-spacing:.04em}}"""


def page(body, w, h, extra="", script=""):
    return (f'<!doctype html><html><head><meta charset="utf-8"><link href="{GF}" rel="stylesheet">'
            f'<style>{CSS} body{{width:{w}px;height:{h}px}} {extra}</style></head><body>'
            f'{brand.grid_svg(w, h)}<div class="wrap">{body}</div>{script}</body></html>')


def head(kicker, title):
    return f'<div class="head">{brand.icon_svg()}<div><div class="kick">{kicker}</div><div class="title">{title}</div></div></div>'


def shoot(name, doc, w, h, jpeg=False):
    with tempfile.TemporaryDirectory() as tmp:
        page_path = os.path.join(tmp, "page.html")
        open(page_path, "w").write(doc)
        png = os.path.join(tmp if jpeg else OUT, f"{name}.png")
        subprocess.run(["google-chrome", "--headless=new", "--disable-gpu", "--hide-scrollbars", "--virtual-time-budget=15000",
                        f"--window-size={w},{h}", f"--screenshot={png}", f"file://{page_path}"], check=True, capture_output=True)
        out = png
        if jpeg:
            out = os.path.join(OUT, f"{name}.jpg")
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", png, "-q:v", "3", out], check=True)
    print("wrote", os.path.relpath(out, ROOT))


def xml_hl(line):
    s = html.escape(line)
    if s.strip().startswith("&lt;!--"):
        return f'<span class="cm">{s}</span>'
    s = re.sub(r'([\w-]+)=(&quot;.*?&quot;)', r'<span class="a">\1</span>=<span class="v">\2</span>', s)
    s = re.sub(r'(&lt;/?)(\w+)', r'\1<span class="t">\2</span>', s)
    return s


# ------------------------------------------------------------------ 01 validator
def card_validate():
    broken = open(os.path.join(SRC, "hunt_actor_typos.xml")).read().splitlines()
    marks = {42: 'strik', 60: 'Wiat', 65: 'far'}
    code = []
    for n in (42, 60, 65):
        s = xml_hl(broken[n - 1].strip())
        s = s.replace(html.escape(marks[n]), f'<span class="bad">{html.escape(marks[n])}</span>', 1)
        code.append(f'<span class="ln">{n}</span>{s}')
    term = "\n".join([
        '<span class="p">$</span> Plugins/AdamskAI/Tools/adsk_validate.sh --paths hunt_actor.xml',
        'hunt_actor.xml:60: <span class="e">error</span> unknown-node: no node type \'Wiat\' in the catalogue',
        'hunt_actor.xml:65: <span class="e">error</span> type-mismatch [chase_move]: attribute value does not match the field type',
        '<span class="c">summary: mode=paths files=1 errors=2 warnings=0 notes=0</span>',
        '<span class="e">FAIL</span> adsk_validate [paths] exit=1',
        '',
        '<span class="c"># fix those two, run again</span>',
        '<span class="p">$</span> Plugins/AdamskAI/Tools/adsk_validate.sh --paths hunt_actor.xml',
        'hunt_actor.xml:42: <span class="e">error</span> unresolved-reference: goto names no state: \'strik\'; <span class="w">did you mean \'strike\'?</span>',
        '<span class="e">FAIL</span> adsk_validate [paths] exit=1',
    ])
    body = (head("adsk_validate · headless, no editor", "Errors <em>before Play</em>, as path:line") +
            f'<div class="panel" style="margin-bottom:22px"><div class="bar"><i></i><i></i><i></i>&nbsp;hunt_actor.xml</div>'
            f'<pre>{chr(10).join(code)}</pre></div>'
            f'<div class="panel"><div class="bar"><i></i><i></i><i></i>&nbsp;bash</div><pre>{term}</pre></div>'
            f'<div class="foot">Real output of Tools/adsk_validate.sh on Examples/hunt_actor.xml with three typos; directory prefix trimmed. Exit 1 fails CI.</div>')
    shoot("validate", page(body, 1600, 780), 1600, 780)


# ------------------------------------------------------------------ 02 simulator
def card_simulate():
    lines = [l for l in open(os.path.join(SRC, "simulate.txt")).read().splitlines() if l.strip()]
    trans = [re.match(r"\s*([\d.]+)\s+(\w+) -> (\w+)\s+\((.+)\)", l) for l in lines]
    trans = [m for m in trans if m]
    duration, segs, cur, t0 = 12.0, [], "search", 0.0
    for m in trans:
        t = float(m.group(1))
        if m.group(3) != cur:
            segs.append((t0, t, cur)); cur, t0 = m.group(3), t
    segs.append((t0, duration, cur))
    col = {"search": AMBER, "chase": RED, "strike": INK}
    W = 1472
    lane = "".join(f'<div style="position:absolute;left:{a / duration * W:.1f}px;width:{(b - a) / duration * W:.1f}px;top:0;bottom:0;'
                   f'background:{col[s]};opacity:.9;border-right:2px solid #050c09;display:flex;align-items:center;justify-content:center;'
                   f'font:700 15px JetBrains Mono;color:#06100b">{s}</div>' for a, b, s in segs)
    ticks = "".join(f'<div style="position:absolute;left:{m_t / duration * W:.1f}px;top:-6px;width:2px;height:62px;background:{GREEN};opacity:.55"></div>'
                    for m_t in (float(m.group(1)) for m in trans if m.group(2) == m.group(3)))
    inputs = [(1.5, "Target = actor"), (2.0, "event Mark"), (8.0, "Target = none")]
    ev = "".join(f'<div style="position:absolute;left:{t / duration * W:.1f}px;top:{24 * (i % 2)}px;transform:translateX(-50%);font:400 14px JetBrains Mono;'
                 f'color:{GREEN};white-space:nowrap">▼ {html.escape(n)}</div>' for i, (t, n) in enumerate(inputs))
    axis = "".join(f'<div style="position:absolute;left:{s / duration * W:.1f}px;font:400 12px JetBrains Mono;color:#5f7a6c">{s}s</div>'
                   for s in range(0, 13, 2))
    shown = lines[:4] + ['<span class="c">   ...    strike -> strike  (tree.success) ×6</span>'] + lines[11:15] + lines[15:]
    term = []
    for l in shown:
        e = l if l.startswith("<span") else html.escape(l)
        e = re.sub(r"(-&gt;)", r'<span class="p">\1</span>', e)
        e = e.replace("PASS", '<span class="ok">PASS</span>')
        term.append(e)
    body = (head("adsk_simulate · seed 1 · 12 s in 360 steps · no editor, no PIE", "What the file <em>decides</em>, step by step") +
            f'<div style="position:relative;height:50px;margin:4px 0 6px">{ev}</div>'
            f'<div style="position:relative;height:50px;border-radius:6px;overflow:hidden">{lane}{ticks}</div>'
            f'<div style="position:relative;height:22px;margin-top:6px">{axis}</div>'
            f'<div class="panel" style="margin-top:12px"><div class="bar"><i></i><i></i><i></i>&nbsp;bash — '
            f'Plugins/AdamskAI/Tools/adsk_simulate.sh Fixtures/scenarios/hunt_actor.json</div><pre style="font-size:16px;line-height:1.45;white-space:pre-wrap">{chr(10).join(term)}</pre></div>'
            f'<div class="foot">Real output of Tools/adsk_simulate.sh on the shipped hunt_actor scenario; the lane is drawn from those lines, thin marks are re-entries.</div>')
    shoot("simulate", page(body, 1600, 880), 1600, 880)


# ------------------------------------------------------------------ 04 benchmark
def card_bench():
    world = [("AdamskAI, brain ticking itself", .08, .11, GREEN), ("AdamskAI, managed pass", .09, .15, GREEN),
             ("Behavior Tree", .12, .13, DIM), ("StateTree", .20, .22, DIM)]
    core = [("AdamskAI core", .13, .15, GREEN), ("StateTree core", 1.31, 1.48, DIM)]

    def chart(rows, top, unit, title):
        W = 900
        out = [f'<div style="font:600 30px Teko;margin:0 0 10px">{title}</div>']
        for name, a, b, c in rows:
            out.append(f'<div style="display:grid;grid-template-columns:380px {W}px 160px;align-items:center;height:46px">'
                       f'<div style="font:400 17px JetBrains Mono;color:{INK if c == GREEN else DIM}">{name}</div>'
                       f'<div style="position:relative;height:22px;background:rgba(62,224,138,.06);border-radius:4px">'
                       f'<div style="position:absolute;left:0;width:{a / top * W:.1f}px;top:0;bottom:0;background:{c};opacity:.35;border-radius:4px 0 0 4px"></div>'
                       f'<div style="position:absolute;left:{a / top * W:.1f}px;width:{max(3, (b - a) / top * W):.1f}px;top:0;bottom:0;background:{c};border-radius:0 4px 4px 0"></div></div>'
                       f'<div style="font:700 17px JetBrains Mono;color:{c};padding-left:16px">{a:.2f}–{b:.2f} {unit}</div></div>')
        return "".join(out)
    body = (head("tools/adsk_bench.sh · 1000 agents · same behavior three ways", "Cheap at rest: <em>µs per agent</em>") +
            f'<div class="panel" style="padding:24px 30px;margin-bottom:20px">{chart(world, .24, "µs", "In a world, per agent and frame")}</div>'
            f'<div class="panel" style="padding:24px 30px">{chart(core, 1.6, "µs", "Cores alone, per tick, no world")}</div>'
            f'<div class="foot">README "Measured": Linux, Ryzen 7 8845HS, Development editor, -nullrhi, 2026-10-31, three runs; bars span min–max. '
            f'Patrol/alert switched every 1.5 s, mostly waiting.</div>')
    shoot("bench", page(body, 1600, 820), 1600, 820)


# ------------------------------------------------------------------ 05 demo frame
def card_demo(shot):
    body = (head("adsk.Demo Tokens · headless capture", "Two strike, the rest <em>wait their turn</em>") +
            f'<div class="panel"><img src="file://{shot}" style="display:block;width:100%"></div>'
            f'<div class="foot">L_AdskDemo, adsk.Demo Tokens + adsk.Demo.Shot 10 · HoldToken on a pool of two: white strikes, purple circles · labels: state / node</div>')
    shoot("demo-tokens", page(body, 1600, 1080), 1600, 1080, jpeg=True)


# ------------------------------------------------------------------ 06 hero frame
def card_hero(shot, crop=None):
    # A game frame as it is: the crop drops a window border, then 1600 px wide like the banner.
    vf = (f"crop={crop}," if crop else "") + "scale=1600:-2:flags=lanczos"
    out = os.path.join(OUT, "hero.jpg")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", shot, "-vf", vf, "-q:v", "3", out], check=True)
    print("wrote", os.path.relpath(out, ROOT))


if __name__ == "__main__":
    args = sys.argv[1:]
    if args[:1] == ["--hero-shot"] and len(args) in (2, 3):
        card_hero(os.path.abspath(args[1]), args[2] if len(args) == 3 else None)
        sys.exit(0)
    card_validate()
    card_simulate()
    card_bench()
    if len(args) == 2 and args[0] == "--demo-shot":
        card_demo(os.path.abspath(args[1]))
    else:
        print("skipped demo-tokens: pass --demo-shot <png>")
    sys.path.insert(0, SRC)
    import statechart
    statechart.v_numbered()
