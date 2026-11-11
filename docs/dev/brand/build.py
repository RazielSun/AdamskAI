"""AdamskAI brand images, rendered by headless Chrome from the HTML and SVG below.

Writes docs/images/banner.png (1600x400, top of the README) and Resources/Icon128.png (the Edit > Plugins icon).
With --social <file.png> it also writes the 1280x640 GitHub social preview, which is uploaded in the repository
settings and is not kept in the repository.

The wordmark is Teko 600 (Google Fonts, SIL OFL), loaded at render time, so rendering needs network access.
The last A is drawn, not typed: a solid view cone with an agent dot, sized from Teko's own cap height and stem.

Usage: python3 -I docs/dev/brand/build.py [--social <file.png>]   (needs google-chrome)
"""
import json, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))

BG0, BG1 = "#0d1f16", "#06100b"            # plate: dark green, centre to edge
INK, GREEN, AMBER = "#e6eee8", "#3ee08a", "#ffb12b"
ICON_BG = "#0c1210"
FONT, WEIGHT, SPACING = "Teko", 600, 0.03   # letter-spacing in em
GF = f"https://fonts.googleapis.com/css2?family={FONT}:wght@{WEIGHT}&display=block"

# The wordmark: ADAMSK, the radar A, then I. Measures the font in the browser so the A matches its weight.
JS = r"""
const C = {bg: '%BG%', ink: '%INK%', green: '%GREEN%', amber: '%AMBER%'};
const FONT = '%FONT%', WEIGHT = %WEIGHT%, SPACING = %SPACING%;

function metrics(size) {
  const c = document.createElement('canvas').getContext('2d');
  c.font = `${WEIGHT} ${size}px "${FONT}"`;
  const H = c.measureText('A').actualBoundingBoxAscent;            // cap height
  const cv = document.createElement('canvas'); cv.width = cv.height = Math.ceil(size * 3);
  const x = cv.getContext('2d'); x.font = c.font; x.fillText('I', size, size * 2);
  const row = x.getImageData(0, Math.round(size * 2 - H / 2), cv.width, 1).data;
  let s = 0; for (let p = 3; p < row.length; p += 4) s += row[p] / 255; // stem: inked pixels across an I
  return {H, s};
}

// Cone as wide as it is tall, cut by an arc centred on the apex so its lowest point sits on the baseline.
function radarA(m, ls) {
  const H = m.H, W = H, cx = W / 2, s = m.s * 0.85;
  const r = s * 0.62, pad = r * 1.4, side = m.s * 0.12;
  const body = `<defs><clipPath id="arc"><circle cx="${cx}" cy="0" r="${H}"/></clipPath></defs>
    <path d="M${cx} 0L${W} ${H}L0 ${H}Z" fill="${C.green}" clip-path="url(#arc)"/>
    <circle cx="${cx}" cy="${-r * 0.15}" r="${r}" fill="${C.amber}" stroke="${C.bg}" stroke-width="${s * 0.22}"/>`;
  return `<svg viewBox="0 ${-pad} ${W} ${H + pad}" width="${W}" height="${H + pad}"
    style="display:inline-block;vertical-align:baseline;overflow:visible;margin:0 ${side + ls}px 0 ${side}px">${body}</svg>`;
}

function render(el, size) {
  const m = metrics(size);
  el.style.font = `${WEIGHT} ${size}px "${FONT}"`;
  el.style.letterSpacing = `${SPACING}em`;
  el.innerHTML = `<span>ADAMSK</span>${radarA(m, SPACING * size)}<span>I</span>`;
}

document.fonts.ready.then(() => {
  for (const el of document.querySelectorAll('.wm')) {
    const max = el.parentElement.clientWidth, want = parseFloat(el.dataset.size);
    render(el, want);
    if (el.scrollWidth > max) render(el, want * max / el.scrollWidth);
  }
});
"""


def grid_svg(w, h):
    """Dark green plate: a vignette, a 16 px grid and an 80 px grid with a tick at each centre."""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" style="position:absolute;inset:0">'
            f'<defs><pattern id="minor" width="16" height="16" patternUnits="userSpaceOnUse">'
            f'<path d="M16 0H0V16" fill="none" stroke="{GREEN}" stroke-opacity=".05"/></pattern>'
            f'<pattern id="major" width="80" height="80" patternUnits="userSpaceOnUse">'
            f'<path d="M80 0H0V80" fill="none" stroke="{GREEN}" stroke-opacity=".11"/>'
            f'<path d="M36 40H44M40 36V44" stroke="{GREEN}" stroke-opacity=".22"/></pattern>'
            f'<radialGradient id="vig" cx="50%" cy="50%" r="75%"><stop offset="0" stop-color="{BG0}"/>'
            f'<stop offset="1" stop-color="{BG1}"/></radialGradient></defs>'
            f'<rect width="{w}" height="{h}" fill="url(#vig)"/><rect width="{w}" height="{h}" fill="url(#minor)"/>'
            f'<rect width="{w}" height="{h}" fill="url(#major)"/></svg>')


def plate_html(w, h, size):
    js = (JS.replace("%BG%", BG0).replace("%INK%", INK).replace("%GREEN%", GREEN).replace("%AMBER%", AMBER)
          .replace("%FONT%", FONT).replace("%WEIGHT%", str(WEIGHT)).replace("%SPACING%", str(SPACING)))
    return (f'<!doctype html><html><head><meta charset="utf-8"><link href="{GF}" rel="stylesheet"><style>'
            f'body{{margin:0;width:{w}px;height:{h}px;position:relative;overflow:hidden;background:{BG1}}}'
            f'.wm{{white-space:nowrap;line-height:1;color:{INK}}}</style></head><body>{grid_svg(w, h)}'
            f'<div style="position:absolute;inset:0;display:flex;align-items:center;justify-content:center;'
            f'padding-top:{h * .04:.0f}px"><div style="width:{w * .78:.0f}px;display:flex;justify-content:center">'
            f'<div class="wm" data-size="{size}"></div></div></div><script>{js}</script></body></html>')


def icon_svg():
    """The radar A alone on a rounded plate, drawn for small sizes (large dot, cone filling the plate)."""
    import math
    top, h, dot = 30, 78, 15
    half = math.atan2(h / 2, h)
    x, y = h * math.sin(half), top + h * math.cos(half)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128">'
            f'<rect width="128" height="128" rx="28" fill="{ICON_BG}"/>'
            f'<path d="M64 {top}L{64 - x:.2f} {y:.2f}A{h} {h} 0 0 0 {64 + x:.2f} {y:.2f}Z" fill="{GREEN}"/>'
            f'<circle cx="64" cy="{top - dot * 0.2:.2f}" r="{dot}" fill="{AMBER}" stroke="{ICON_BG}" '
            f'stroke-width="{dot * 0.35:.2f}"/></svg>')


def shoot(html, w, h, out, transparent=False):
    with tempfile.TemporaryDirectory() as tmp:
        page = os.path.join(tmp, "page.html")
        open(page, "w").write(html)
        cmd = ["google-chrome", "--headless=new", "--disable-gpu", "--hide-scrollbars", "--virtual-time-budget=10000",
               f"--window-size={w},{h}", f"--screenshot={out}", f"file://{page}"]
        if transparent:
            cmd.insert(1, "--default-background-color=00000000")
        subprocess.run(cmd, check=True, capture_output=True)
    print("wrote", os.path.relpath(out, ROOT))


def main(argv):
    if not shutil.which("google-chrome"):
        sys.exit("google-chrome is required")
    open(os.path.join(HERE, "icon.svg"), "w").write(icon_svg() + "\n")
    shoot(plate_html(1600, 400, 200), 1600, 400, os.path.join(ROOT, "docs", "images", "banner.png"))
    shoot('<!doctype html><html><head><style>html,body{margin:0;background:transparent}'
          'svg{display:block;width:128px;height:128px}</style></head><body>' + icon_svg() + '</body></html>',
          128, 128, os.path.join(ROOT, "Resources", "Icon128.png"), transparent=True)
    if len(argv) == 2 and argv[0] == "--social":
        shoot(plate_html(1280, 640, 230), 1280, 640, os.path.abspath(argv[1]))


if __name__ == "__main__":
    main(sys.argv[1:])
