# XML style

How a behavior file is laid out so a person can scan it and an agent can edit it without breaking it.
The compiler ignores all of this: comments and whitespace are dropped before validation, so style never
changes a definition. Legal syntax is in [xml-reference.md](xml-reference.md); copy the layout from
[../Examples/](../Examples/README.md).

## Layout

1. **Header comment** before `<Agent>`: the file id, which pawn/controller hosts it, the behavior loop in
   one or two lines (`idle -> chase -> attack -> ...`), and the units (`duration` seconds,
   `acceptanceRadius` cm).
2. **Sections in schema order**, each under a banner comment and separated by one blank line:
   `Enums` (when used), `Inputs`, `Locals`, `Sensors` (when used), `Events`, `State machine`, `Trees`.
   ```xml
   <!-- ========== Inputs ========== -->
   ```
3. **One blank line between `State`s and between `Tree`s.** None inside a state or tree.
4. **Trees follow state order.** When a state runs exactly one tree, give the tree the state's id.
5. **Two-space indent, one element per line, self-closing leaves** (`<Wait id="w" duration="1"/>`).

## Comments

- **One line above each `State`** saying its intent and anything non-obvious about its exits
  ("a blocked line of fire fails into reposition"). Do not restate the `On` rows; they are the
  source of truth.
- Comment a tuning value only when the reason is not obvious from the name.
- Comments are never semantics: nothing the runtime needs may live in one.
- XML forbids `--` inside a comment; use `->` or a single hyphen.

## Names and attributes

- **Node ids are prefixed with their tree id**: `chase_move`, `attack_swing`. Diagnostics and traces
  print the node id alone, and the prefix says where it lives.
- **Attribute order:** `id`, then `in-` / `out-` port bindings, then configuration fields in catalogue
  order. Example: `<MoveTo id="search_move" in-goal="LastKnown" acceptanceRadius="100"/>`.
- **`State` attribute order:** `id`, then `initial` or `tree`, then `tags`. Tag the outermost state a tag is true for
  (`AI.State.Combat` on `engage`, not on each child), and children only with what they add (`AI.Action.Attack`).
- **Transition order inside a `State`:** declared events first, then `tree.success`, then `tree.failure`. Conditional
  handlers for one event keep their decision order, with the unconditional fallback last; a handler after it is
  `unreachable-transition`.
- **A `Run` call-site id names what it does** (`reload1`): copied nodes print as `reload1/<node id>`, so keep tree-prefixed
  ids short and do not author ids containing `/` that could collide.
- Keep a line within 110 characters. A longer element puts each attribute on its own line, indented
  two spaces past the element name.

## Edit safety

- **Keep each leaf element on one line.** Agents and tests edit tuning by an exact one-line match (for
  example `<Wait id="idle_wait" duration="0.5"/>`); wrapping a short leaf breaks that match.
- Never reformat [../Fixtures/](../Fixtures/): their `.expect` files pin diagnostic line numbers.
- Re-run `Plugins/AdamskAI/Tools/adsk_validate.sh` after every edit, including comment-only ones.
