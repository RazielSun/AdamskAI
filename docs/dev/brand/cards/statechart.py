"""The statechart card: hunt_actor's transitions, numbered in the XML and on the arrows. Edges from hunt.dot
(Tools/adsk_graph.sh Examples/hunt_actor.xml dot). Run through cards.py."""
import html, json, os, re, sys

SRC = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SRC)
import cards as b  # noqa: E402

INK, GREEN, AMBER, RED, DIM = b.INK, b.GREEN, b.AMBER, b.RED, b.DIM
XML = os.path.join(b.ROOT, "Examples/hunt_actor.xml")

# The transitions, in file order, from adsk_graph's DOT output.
EDGES = [(m.group(1), m.group(2), m.group(3)) for m in
         re.finditer(r'"(\w+)" -> "(\w+)" \[label="([^"]+)"\]', open(os.path.join(SRC, "hunt.dot")).read())]
STATES = ["search", "chase", "strike"]
TREES = {"search": "search", "chase": "chase", "strike": "strike"}


def kind(label):
    return "fail" if label.startswith("tree.failure") else "change" if label.startswith("TargetChanged") else "ok"


KCOL = {"ok": GREEN, "change": AMBER, "fail": RED}


def merged():
    """(src, dst) -> [(index, label)], in file order."""
    out = {}
    for i, (s, d, l) in enumerate(EDGES, 1):
        out.setdefault((s, d), []).append((i, l))
    return out


def xml_lines(badges=False):
    """The state machine with every <On> prefixed by its transition number."""
    src = [l[2:] for l in open(XML).read().splitlines()[27:55] if "Bind" not in l and "<!--" not in l and l.strip()]
    out, n = [], 0
    for l in src:
        h = b.xml_hl(l)
        if badges and "goto=" in l:
            n += 1
            c = KCOL[kind(EDGES[n - 1][2])]
            h = (f'<span style="display:inline-block;width:26px;height:26px;border-radius:50%;background:{c};color:#06100b;'
                 f'font:700 14px/26px JetBrains Mono;text-align:center;margin-right:10px;vertical-align:middle">{n}</span>') + h
        elif badges:
            h = '<span style="display:inline-block;width:36px"></span>' + h
        out.append(h)
    return "\n".join(out)


def panel(title, inner, style=""):
    return (f'<div class="panel" style="{style}"><div class="bar"><i></i><i></i><i></i>&nbsp;{title}</div>{inner}</div>')


FOOT = "Examples/hunt_actor.xml, its state machine (Mark handlers omitted); edges from Tools/adsk_graph.sh."


# ------------------------------------------------------------------ shared: a hand-laid three-state diagram
def diagram(W, H, cx, cy, node_w, node_h, inner=None, numbers=True, label_size=14, view=None):
    """search, chase, strike on one line; forward arcs above, back arcs below, loops on top. Returns SVG."""
    xs = dict(zip(STATES, cx))
    m = merged()
    top, bot = cy - node_h / 2, cy + node_h / 2
    arcs = {  # (src, dst): (start, control, end)
        ("search", "chase"): ((xs["search"] + node_w * .3, top), ((xs["search"] + xs["chase"]) / 2, top - 120), (xs["chase"] - node_w * .3, top)),
        ("chase", "strike"): ((xs["chase"] + node_w * .3, top), ((xs["chase"] + xs["strike"]) / 2, top - 120), (xs["strike"] - node_w * .3, top)),
        ("chase", "search"): ((xs["chase"] - node_w * .3, bot), ((xs["search"] + xs["chase"]) / 2, bot + 110), (xs["search"] + node_w * .3, bot)),
        ("strike", "chase"): ((xs["strike"] - node_w * .3, bot), ((xs["chase"] + xs["strike"]) / 2, bot + 110), (xs["chase"] + node_w * .3, bot)),
        ("strike", "search"): ((xs["strike"], bot), (xs["chase"], bot + 330), (xs["search"], bot)),
    }
    g = [f'<defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
         f'<path d="M0 0L10 5L0 10z" fill="#7fd6a8"/></marker></defs>']

    def badges(x, y, items):
        out = ""
        n = len(items)
        for k, (i, l) in enumerate(items):
            bx = x + (k - (n - 1) / 2) * 34
            c = KCOL[kind(l)]
            if numbers:
                out += (f'<circle cx="{bx:.1f}" cy="{y:.1f}" r="15" fill="{c}" stroke="#06100b" stroke-width="3"/>'
                        f'<text x="{bx:.1f}" y="{y + 5:.1f}" text-anchor="middle" font-family="JetBrains Mono" font-weight="700" font-size="15" fill="#06100b">{i}</text>')
        if not numbers:
            for k, (i, l) in enumerate(items):
                out += (f'<text x="{x:.1f}" y="{y + (k - (n - 1) / 2) * (label_size + 6) + 5:.1f}" text-anchor="middle" '
                        f'font-family="JetBrains Mono" font-size="{label_size}" fill="{KCOL[kind(l)]}" paint-order="stroke" '
                        f'stroke="#06100b" stroke-width="6">{html.escape(l)}</text>')
        return out
    for key, (p0, c, p2) in arcs.items():
        g.append(f'<path d="M{p0[0]:.1f} {p0[1]:.1f}Q{c[0]:.1f} {c[1]:.1f} {p2[0]:.1f} {p2[1]:.1f}" fill="none" stroke="#5fbf8f" '
                 f'stroke-width="2" marker-end="url(#ah)"/>')
        mx, my = (p0[0] + 2 * c[0] + p2[0]) / 4, (p0[1] + 2 * c[1] + p2[1]) / 4
        g.append(badges(mx, my, m[key]))
    for s in ("search", "strike"):  # self loops on top
        x = xs[s]
        g.append(f'<path d="M{x - 28} {top}C{x - 70} {top - 110} {x + 70} {top - 110} {x + 28} {top}" fill="none" stroke="#5fbf8f" '
                 f'stroke-width="2" marker-end="url(#ah)"/>')
        g.append(badges(x, top - 92, m[(s, s)]))
    # initial
    g.append(f'<circle cx="{xs["search"] - node_w / 2 - 70}" cy="{cy}" r="9" fill="{GREEN}"/>'
             f'<path d="M{xs["search"] - node_w / 2 - 60} {cy}H{xs["search"] - node_w / 2 - 4}" stroke="#5fbf8f" stroke-width="2" marker-end="url(#ah)"/>')
    for s in STATES:
        x = xs[s]
        g.append(f'<rect x="{x - node_w / 2}" y="{top}" width="{node_w}" height="{node_h}" rx="14" fill="#10241a" stroke="{GREEN}" stroke-width="2"/>')
        if inner:
            g.append(inner(s, x, top, node_w, node_h))
        else:
            g.append(f'<text x="{x}" y="{cy + 2}" text-anchor="middle" font-family="JetBrains Mono" font-weight="700" font-size="22" fill="{INK}">{s}</text>'
                     f'<text x="{x}" y="{cy + 24}" text-anchor="middle" font-family="JetBrains Mono" font-size="13" fill="{DIM}">tree {s}</text>')
    vb = view or (0, 0, W, H)
    return f'<svg viewBox="{vb[0]} {vb[1]} {vb[2]} {vb[3]}" style="width:100%;height:100%">{"".join(g)}</svg>'


# ------------------------------------------------------------------ 03b numbered: each line is an arrow
def v_numbered():
    svg = diagram(760, 600, [190, 420, 650], 250, 150, 64, view=(30, 95, 710, 385))
    legend = "".join(f'<span style="display:inline-flex;align-items:center;gap:8px;margin-right:22px">'
                     f'<i style="width:14px;height:14px;border-radius:50%;background:{c}"></i>{t}</span>'
                     for c, t in ((GREEN, "tree.success"), (AMBER, "TargetChanged"), (RED, "tree.failure")))
    extra = ".cols{display:grid;grid-template-columns:1.05fr 1fr;gap:22px;height:640px} .cols .panel{height:100%}"
    body = (b.head("adsk_graph · the file is the source", "Every <em>line</em> is an arrow") +
            f'<div class="cols">{panel("Examples/hunt_actor.xml", f"<pre style=\"font-size:15px;line-height:1.62\">{xml_lines(True)}</pre>")}'
            f'{panel("the same ten transitions", f"<div style=\"height:calc(100% - 100px);padding:24px 20px 0\">{svg}</div>"
                     f"<div style=\"padding:6px 24px;font:400 15px JetBrains Mono;color:{DIM}\">{legend}</div>")}</div>'
            f'<div class="foot">{FOOT} Numbers follow the file order of the On lines.</div>')
    b.shoot("statechart", b.page(body, 1600, 860, extra), 1600, 860)


