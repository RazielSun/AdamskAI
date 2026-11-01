# References

Where the ideas in AdamskAI come from, and what to read for the background. How AdamskAI relates to each system is
in [concept.md](concept.md#relatives) and, feature by feature, in [comparison.md](comparison.md); the terms side by
side are in [glossary.md](glossary.md).

## Statecharts

- David Harel, *Statecharts: A visual formalism for complex systems*, Science of Computer Programming 8(3), 1987,
  pp. 231–274. Hierarchical states, history and events: the model behind `StateMachine`, `State`, `initial` and
  `history`.
- W3C, *State Chart XML (SCXML): State Machine Notation for Control Abstraction*, Recommendation, 2015,
  <https://www.w3.org/TR/scxml/>. An XML statechart language; AdamskAI's transitions, priorities and history follow
  its semantics where they overlap.

## Behavior trees

- Michele Colledanchise and Petter Ögren, *Behavior Trees in Robotics and AI: An Introduction*, CRC Press, 2018;
  preprint <https://arxiv.org/abs/1709.00084>. Formal semantics of sequences, fallbacks, parallels and decorators.
- BehaviorTree.CPP (v4), <https://www.behaviortree.dev/>, source <https://github.com/BehaviorTree/BehaviorTree.CPP>.
  XML-authored trees with typed ports bound to blackboard entries and subtrees with remapping, the closest relative
  of AdamskAI's trees, ports and `Run`.
- Damian Isla, *Handling Complexity in the Halo 2 AI*, Game Developers Conference, 2005. Behavior trees in games,
  with modes on top and procedures underneath.
- py_trees, <https://github.com/splintered-reality/py_trees>. Explicit memory/reactive control semantics,
  parallel completion policies, blackboard access registration and an activity stream.

## Hybrid composition and authoring

- LimboAI, *BTState*, <https://limboai.readthedocs.io/en/stable/classes/class_btstate.html>.
  An HSM state owns a behavior tree and dispatches events on its success or failure: the closest structural
  analogue to AdamskAI's modes and procedures.
- NodeCanvas, <https://nodecanvas.paradoxnotion.com/>. Hierarchical state machines containing behavior trees,
  reusable subgraphs and explicit variable mapping; a composition and tooling reference, not an XML runtime.
- CryEngine, *Modular Behavior Tree*,
  <https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756813/pages/23306488>.
  XML-authored native behaviors and separate per-agent runtime data.
- Unity Behavior, <https://docs.unity3d.com/Packages/com.unity.behavior@1.0/manual/behavior-features.html>.
  Domain actions, a common action lifecycle and reusable subgraphs.
- HFSM2, <https://github.com/andrew-gresyk/HFSM2>. Hierarchical and orthogonal regions, utility selection,
  transition inspection and configuration serialization; its compile-time machine differs from loaded XML.

## Unreal Engine

- Behavior Trees in Unreal Engine, <https://dev.epicgames.com/documentation/en-us/unreal-engine/behavior-trees-in-unreal-engine>.
- StateTree, <https://dev.epicgames.com/documentation/en-us/unreal-engine/state-tree-in-unreal-engine>. States that
  run tasks, transitions on completion and events, utility and weighted selection: AdamskAI's execution model.
- Environment Query System, <https://dev.epicgames.com/documentation/en-us/unreal-engine/environment-query-system-in-unreal-engine>,
  which `RunEQS` runs.

## Utility and other decision models

- Dave Mark, *Behavioral Mathematics for Game AI*, Course Technology, 2009. Response curves and utility scoring:
  `Score`, `UtilitySelector` and `select="best"`.
- Jeff Orkin, *Three States and a Plan: The A.I. of F.E.A.R.*, Game Developers Conference, 2006. Goal-oriented
  action planning, the approach AdamskAI does not take: it decides by authored modes and procedures, and leaves
  long-horizon planning to a node.
- Tim Johan Verweij, *From Byrd Box to Debug Boxes: HTN Introduction and Application in DECIMA*, 2026,
  <https://www.guerrilla-games.com/read/from-byrd-box-to-debug-boxes-htn-introduction-and-application-in-decima>.
  Authored methods, backtracking over preconditions and debugging decompositions. A planning and explanation
  reference, not a description of AdamskAI's execution model.
- ReGoap, <https://github.com/luxkun/ReGoap>. A concrete GOAP reference for preconditions, effects and plan search;
  not a selected dependency.

Links were current when written; titles and venues are given so a moved page can still be found.
