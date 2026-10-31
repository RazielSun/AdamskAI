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

Links were current when written; titles and venues are given so a moved page can still be found.
