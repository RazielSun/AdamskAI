# Brand

The wordmark is `ADAMSK`, a drawn **A** and `I`, in Teko 600 (Google Fonts, SIL Open Font License). The A is a
radar view cone with an agent dot on its apex, a nod to the stealth games the name comes from; the cone is green
(`#3ee08a`), the dot amber (`#ffb12b`), the letters off-white (`#e6eee8`) on dark green (`#0d1f16` to `#06100b`).

[build.py](build.py) renders every image from HTML and SVG with headless Chrome; nothing is drawn by hand:

| Output | Size | Used by |
|---|---|---|
| `docs/images/banner.png` | 1600×400 | the top of [README.md](../../../README.md) |
| `Resources/Icon128.png` | 128×128, transparent corners | Edit > Plugins in the editor |
| [icon.svg](icon.svg) | vector | the icon's master; the PNG above is rendered from it |
| `--social <file.png>` | 1280×640 | the GitHub social preview (Settings > General > Social preview); not kept here |

```bash
python3 -I docs/dev/brand/build.py [--social <file.png>]
```

It needs `google-chrome` and network access (the font loads from Google Fonts). The images carry no version or
test count, so they do not go stale with a release.

## Tool cards

[cards/cards.py](cards/cards.py) and [cards/statechart.py](cards/statechart.py) draw the pictures in the README and docs
from real tool output, in the same style:

| Output | From | Used by |
|---|---|---|
| `docs/images/statechart.png` | `Examples/hunt_actor.xml` and [cards/hunt.dot](cards/hunt.dot) (`Tools/adsk_graph.sh Examples/hunt_actor.xml dot`) | README, *What a behavior looks like* |
| `docs/images/validate.png` | `adsk_validate.sh --paths` on [cards/hunt_actor_typos.xml](cards/hunt_actor_typos.xml) (`Wiat`, `far`, `strik`), output typed into `cards.py` | README, *Quick start* |
| `docs/images/simulate.png` | [cards/simulate.txt](cards/simulate.txt) (`adsk_simulate.sh Fixtures/scenarios/hunt_actor.json`) | [testing.md](../testing.md#simulation) |
| `docs/images/bench.png` | the README's *Measured* table, typed into `cards.py` | README, *Measured* |
| `docs/images/demo-tokens.jpg` | a screenshot from `adsk.Demo Tokens, adsk.Labels.Scale 1.6, adsk.Demo.Camera 0 -1000 750 -35 90 55, adsk.Demo.Shot 10 tokens clean quit` ([demo.md](../../demo.md#screenshots)) | README, *What a behavior looks like*; [demo.md](../../demo.md) |

```bash
python3 -I docs/dev/brand/cards/cards.py --demo-shot Saved/Screenshots/<Platform>/AdamskAI/tokens.png
```

The benchmark card carries numbers: when the README's *Measured* table changes, change `card_bench` with it and
render again. When a tool's output changes, run it again and replace the text file it came from.

Two runs do not give byte-identical PNGs: the fonts load from Google Fonts and Chrome's antialiasing differs by a few
pixels, so compare a new card by eye, not by hash. The demo frame differs more: a game run's frame times vary, so the
agents stand elsewhere each time; keep a frame where one of them strikes (white).
