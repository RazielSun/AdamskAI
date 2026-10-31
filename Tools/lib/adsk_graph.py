# The state machine of a behavior file as a diagram, shared by Tools/adsk_graph.sh and Tools/adsk_graph_win64.ps1.
# Usage: python3 -I adsk_graph.py <file.xml> [mermaid|dot]. Mermaid renders in GitHub Markdown inside a ```mermaid
# block; DOT renders with Graphviz (`dot -Tsvg`). It reads the file as written and checks nothing: validate first.
# Exit codes: 0 written, 2 usage or a file that is not an Agent.

import sys
import xml.etree.ElementTree as ET


def condition(on):
    """The handler's guard as text: `if Target is none`, `unless Alert`, `60%`."""
    parts = []
    for key in ("if", "unless"):
        if key in on.attrib:
            text = f"{key} {on.attrib[key]}"
            for op in ("is", "below", "above"):
                if op in on.attrib:
                    text += f" {op} {on.attrib[op]}"
            parts.append(text)
    if on.find("When") is not None or on.find("Any") is not None:
        parts.append("when ...")
    if "chance" in on.attrib:
        chance = on.attrib["chance"]
        try:
            parts.append(f"{float(chance) * 100:g}%")
        except ValueError:
            parts.append(f"chance {chance}")
    return ", ".join(parts)


def label(on):
    text = on.attrib.get("event", "?")
    guard = condition(on)
    return f"{text} [{guard}]" if guard else text


class Graph:
    def __init__(self, root):
        self.machine = root.find("StateMachine")
        if self.machine is None:
            raise ValueError("no StateMachine")
        self.edges = []
        self.collect(self.machine)

    def collect(self, state):
        for on in state.findall("On"):
            if "goto" in on.attrib:
                self.edges.append((state.attrib["id"], on.attrib["goto"], label(on)))
        for child in state.findall("State"):
            self.collect(child)

    @staticmethod
    def entry(state):
        """How a parent picks its child: `initial`, a `select` mode, and `history`."""
        notes = []
        if "select" in state.attrib:
            notes.append(f"select={state.attrib['select']}")
        if "history" in state.attrib:
            notes.append(f"history={state.attrib['history']}")
        return notes

    def mermaid(self):
        lines = ["stateDiagram-v2"]

        def emit(state, depth):
            pad = "  " * depth
            children = state.findall("State")
            sid = state.attrib["id"]
            if children:
                if state is not self.machine:
                    lines.append(f"{pad}state {sid} {{")
                inner = pad + ("  " if state is not self.machine else "")
                lines.append(f"{inner}[*] --> {state.attrib.get('initial', children[0].attrib['id'])}")
                for note in self.entry(state):
                    lines.append(f"{inner}note right of {state.attrib.get('initial', children[0].attrib['id'])} : {sid} {note}")
                for child in children:
                    emit(child, depth + (0 if state is self.machine else 1))
                if state is not self.machine:
                    lines.append(f"{pad}}}")
            elif "tree" in state.attrib:
                lines.append(f"{pad}{sid} : tree {state.attrib['tree']}")
            else:
                # Declared inside its parent, so an edge naming it does not create it at the top level.
                lines.append(f"{pad}{sid}")

        emit(self.machine, 1)
        for source, target, text in self.edges:
            lines.append(f"  {source} --> {target} : {text}")
        return "\n".join(lines) + "\n"

    def dot(self):
        lines = ["digraph agent {", "  compound=true;", "  node [shape=box, style=rounded];"]

        def emit(state, depth):
            pad = "  " * depth
            children = state.findall("State")
            sid = state.attrib["id"]
            if children:
                notes = ", ".join([f"initial={state.attrib.get('initial', '')}"] + self.entry(state))
                lines.append(f'{pad}subgraph "cluster_{sid}" {{')
                lines.append(f'{pad}  label="{sid} ({notes})";')
                for child in children:
                    emit(child, depth + 1)
                lines.append(f"{pad}}}")
            else:
                tree = f"\\ntree {state.attrib['tree']}" if "tree" in state.attrib else ""
                lines.append(f'{pad}"{sid}" [label="{sid}{tree}"];')

        emit(self.machine, 1)
        leaves = {s.attrib["id"]: s for s in self.machine.iter("State") if s.find("State") is None}
        parents = {s.attrib["id"]: s for s in self.machine.iter("State") if s.find("State") is not None}
        parents[self.machine.attrib["id"]] = self.machine

        def anchor(sid):
            """A leaf inside a parent, so an edge to or from a parent can attach to its cluster."""
            state = parents.get(sid)
            while state is not None and state.find("State") is not None:
                state = next((c for c in state.findall("State") if c.attrib["id"] == state.attrib.get("initial")), state.findall("State")[0])
            return state.attrib["id"] if state is not None else sid

        for source, target, text in self.edges:
            attrs = [f'label="{text}"']
            if source in parents:
                attrs.append(f'ltail="cluster_{source}"')
            if target in parents:
                attrs.append(f'lhead="cluster_{target}"')
            src = source if source in leaves else anchor(source)
            dst = target if target in leaves else anchor(target)
            lines.append(f'  "{src}" -> "{dst}" [{", ".join(attrs)}];')
        lines.append("}")
        return "\n".join(lines) + "\n"


def main(argv):
    if len(argv) < 2 or len(argv) > 3 or (len(argv) == 3 and argv[2] not in ("mermaid", "dot")):
        print("usage: adsk_graph.py <file.xml> [mermaid|dot]", file=sys.stderr)
        return 2
    try:
        root = ET.parse(argv[1]).getroot()
        if root.tag != "Agent":
            raise ValueError("the root element is not Agent")
        graph = Graph(root)
    except (OSError, ET.ParseError, ValueError, KeyError) as error:
        print(f"ERROR: {argv[1]}: {error}", file=sys.stderr)
        return 2
    sys.stdout.write(graph.dot() if len(argv) == 3 and argv[2] == "dot" else graph.mermaid())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
