# MORPH-ECS Research Track

This directory tracks the research question associated with KairoECS. The v1
runtime remains a stable sparse-set implementation; the research branch must
earn any adaptive layout change experimentally rather than rewriting production
storage first.

## Application problem

Interactive worlds alternate between regimes. Combat/crowd frames may repeatedly
iterate very large stable component sets, while streaming, spawning, destruction
and editor-driven simulation cause bursts of structural mutation. Sparse-set and
archetype/chunk layouts optimize different sides of that tradeoff.

## Research question

Can a runtime classify recent query/mutation behavior and migrate selected
component groups between sparse and chunk/archetype-like storage so that total
frame cost is lower than either fixed layout across heterogeneous workloads?

## Novel hypothesis

A bounded online cost model using query frequency, matched-entity count,
structural-churn rate, measured iteration time and measured migration cost can
choose a storage policy with hysteresis that captures stable-iteration locality
without thrashing during mutation bursts.

The novelty is **not** sparse sets or archetypes. The claim lives or dies on the
online policy, migration boundary, bounded overhead, and heterogeneous-workload
results.

## Required baselines

1. Current Kairo sparse set.
2. Fixed archetype/chunk reference implementation.
3. Fixed hybrid policy selected offline.
4. MORPH adaptive policy.
5. Oracle/offline best-per-phase policy for upper-bound context.

## Workload families

Normal: stable transform/velocity iteration, gameplay tags, physics-facing
runtime components. Structural: spawn/despawn bursts, add/remove components,
streamed-zone activation. Mixed: alternating 5-30 second stable phases and
mutation bursts. Adversarial: oscillating workloads designed to trigger policy
thrash, tiny worlds where adaptation overhead dominates, and component
distributions with one very rare query term.

Entity scales: 1k, 10k, 50k, 100k, and a larger scale only when memory allows.

## Metrics

CPU time per query and structural operation, p50/p95/p99 frame contribution,
migration time, bytes moved, peak memory, cache counters where portable tooling
allows, policy decisions, hysteresis reversals, and end-to-end runtime frame
time.

## Failure criteria

The adaptive policy fails its central claim if migration overhead erases gains
on mixed workloads, if it oscillates under adversarial phase changes, if its
memory overhead is materially worse without performance benefit, or if a fixed
policy dominates the representative workload set.

## Required figures

Storage diagrams; phase timeline; policy-decision plot; iteration-versus-churn
tradeoff; p99 latency; migration-cost ablation; memory/performance Pareto;
counter-case figure where a fixed baseline wins.

All plotted numbers must be generated from raw benchmark output. See
`research.yaml` for machine-readable track state.
