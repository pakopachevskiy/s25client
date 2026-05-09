# AI Runtime Profiler Extension Plan

## Goal

Extend the existing AI JH runtime profiling so late-game `AIPlayerJH::RunGF()`
cost can be inspected both:

- as full-run aggregated totals at shutdown, and
- as rolling per-log-window averages in `ai_performance.csv`

The work should reuse the existing `AIRuntimeProfiler` and align
`AIPerfReporter` output with the same profiled sections.

This is a planning document only. No code changes are included here.

## Motivation

`AIPlayerJH::RunGF()` in
`libs/s25main/ai/aijh/runtime/AIPlayerJH.cpp` executes every game frame and
contains a mix of:

- always-on work,
- periodic work every 100/150/500/1000/1500 frames,
- and deeper AI job execution that can scale badly in the late game.

The current profiling setup is useful but incomplete for the stated task:

- `RunGF()` has several top-level spans already, but not a total `RunGF` span.
- `AIRuntimeProfiler` only reports process-lifetime totals on shutdown.
- `AIPerfReporter` logs a small CSV, but its columns are unrelated to the
  existing runtime span breakdown.

That means it is still hard to answer the practical question:
"During a late-game window, which parts of `RunGF()` consume the most average
time?"

## Current Relevant Flow

### Existing span instrumentation

- `AIPlayerJH::RunGF()` already wraps several sections in
  `ScopedAIRuntimeProfile`.
- Deeper instrumentation already exists in:
  - `AIEventHandler`
  - `AIConstruction`
  - `AIMilitaryLogistics`
  - `TargetSelectorAttrition`
  - global build job execution

### Existing runtime profiler behavior

- `AIRuntimeProfiler` stores per-section:
  - `calls`
  - `totalNs`
  - `maxNs`
  - `totalWorkUnits`
  - `maxWorkUnits`
- It is enabled by `RTTR_AI_PROFILE`.
- It prints a sorted shutdown summary in its destructor.

### Existing periodic perf logging

- `AIPerfReporter::MaybeLog()` writes `ai_performance.csv` every `1000` GF.
- It currently logs:
  - `GameFrame`
  - `ElapsedMillis`
  - `GlobalPositionSearchInvocations`
  - `GlobalPositionSearchCooldownSkips`

This CSV is the right place to emit per-window averages for the runtime spans.

## Recommended Design

### 1. Keep one profiling source of truth

Do not add a second independent timing system for `RunGF`.

Instead:

- extend `AIRuntimeProfiler` so it can expose section counters to the reporter,
- keep `ScopedAIRuntimeProfile` as the only span mechanism,
- let `AIPerfReporter` compute window deltas from profiler snapshots.

This avoids drift between shutdown totals and CSV output.

### 2. Distinguish lifetime totals from log-window averages

Keep the existing shutdown totals exactly because they are useful for full-run
ranking.

Add a snapshot/delta API so `AIPerfReporter` can compute, per log window:

- section call delta,
- section total time delta,
- average microseconds per call within the window,
- average microseconds per game frame within the window.

For this task, the most important metric is average cost during gameplay, so
the CSV should emphasize per-window cost, not just per-call average.

### 3. Add an explicit total `RunGF` section

Introduce a new `AIRuntimeProfileSection::RunGF` and place a
`ScopedAIRuntimeProfile` at the top of `AIPlayerJH::RunGF()`.

That gives:

- total cost of one AI tick,
- a ceiling against which child section totals can be compared,
- and a direct late-game signal even when work moves between subphases.

The span should begin after trivial function entry is reached and cover the
entire method body, including early returns.

### 4. Align CSV columns with runtime spans

`AIPerfReporter` should stop being a one-off file with unrelated counters.

Recommended CSV layout:

- always include core context columns first,
- then emit runtime section columns in a stable order derived from
  `AIRuntimeProfileSection`,
- then emit extra non-span counters such as global search counts.

Recommended first columns:

- `GameFrame`
- `ElapsedMillis`
- `WindowGameFrames`
- `RunGF_AvgUsPerGF`
- `RunGF_AvgUsPerCall`

Then, for each selected runtime section:

- `<SectionName>_AvgUsPerGF`
- `<SectionName>_AvgUsPerCall`
- `<SectionName>_Calls`

Then preserve the existing counters:

- `GlobalPositionSearchInvocations`
- `GlobalPositionSearchCooldownSkips`

This keeps the CSV centered on the same spans used by the shutdown summary.

## Scope of Instrumentation Changes

### `RunGF()` top-level spans

The current top-level coverage is already decent. The plan should keep the
existing sections and add:

- `RunGF`

No other new `RunGF()` top-level sections are required immediately unless a
currently unprofiled block becomes important during implementation review.

### Existing sections to surface in CSV

At minimum, include these sections in the CSV because they are already directly
relevant to `RunGF()`:

- `RunGF`
- `RefreshBuildingQualities`
- `BuildingPlannerUpdate`
- `ExecuteAIJob`
- `EvaluateCaptureRisks`
- `TryToAttack`
- `TrySeaAttack`
- `CheckEconomicHotspots`
- `UpdateTroopsLimit`
- `AdjustSettings`
- `PlanNewBuildings`

It is acceptable to also include deeper existing sections, but the first
version should prioritize columns that map directly to the visible `RunGF()`
control flow.

## Concrete Implementation Steps

### 1. Extend `AIRuntimeProfileSection`

Files:

- `libs/s25main/ai/aijh/debug/AIRuntimeProfiler.h`
- `libs/s25main/ai/aijh/debug/AIRuntimeProfiler.cpp`

Changes:

- add `RunGF` to `AIRuntimeProfileSection`, preferably as the first entry,
- add its printable name to the section-name table,
- ensure shutdown summary includes it naturally.

Why first:

- it makes the total tick cost easy to find in enum order and CSV order,
- and avoids hiding it among lower-level phases.

### 2. Add profiler snapshot access for reporters

Files:

- `libs/s25main/ai/aijh/debug/AIRuntimeProfiler.h`
- `libs/s25main/ai/aijh/debug/AIRuntimeProfiler.cpp`

Changes:

- expose a lightweight read-only snapshot type for section stats,
- add an accessor that returns current accumulated stats for all sections,
- keep ownership inside `AIRuntimeProfiler`; `AIPerfReporter` should only read.

Recommended API shape:

```cpp
struct AIRuntimeSectionSnapshot
{
    std::uint64_t calls;
    std::uint64_t totalNs;
    std::uint64_t maxNs;
    std::uint64_t totalWorkUnits;
    std::uint64_t maxWorkUnits;
};

using AIRuntimeSnapshot = std::array<AIRuntimeSectionSnapshot, static_cast<unsigned>(AIRuntimeProfileSection::Count)>;

AIRuntimeSnapshot GetSnapshot() const;
```

The reporter can then keep the previous snapshot and subtract to obtain window
metrics.

### 3. Instrument total `RunGF`

File:

- `libs/s25main/ai/aijh/runtime/AIPlayerJH.cpp`

Changes:

- create a `ScopedAIRuntimeProfile runGfProfile(AIRuntimeProfileSection::RunGF);`
  at the beginning of `AIPlayerJH::RunGF()`,
- ensure it covers all early returns and all existing sub-blocks.

This provides the primary metric requested by the task.

### 4. Align `AIPerfReporter` header with runtime sections

Files:

- `libs/s25main/ai/aijh/debug/AIPerfReporter.h`
- `libs/s25main/ai/aijh/debug/AIPerfReporter.cpp`

Changes:

- store the previous runtime profiler snapshot in `AIPerfReporter`,
- define the ordered subset of sections that should be emitted into CSV,
- build the CSV header from those sections,
- remove the current special-case header layout.

Recommended reporter state additions:

- previous profiler snapshot,
- previous logged game frame,
- helper methods for CSV header generation and section-row generation.

### 5. Compute per-window averages in `AIPerfReporter::MaybeLog()`

Files:

- `libs/s25main/ai/aijh/debug/AIPerfReporter.cpp`

Changes:

- on each log point, grab a fresh profiler snapshot,
- compute deltas against the last snapshot,
- derive:
  - `windowGameFrames`
  - `avgUsPerGF = deltaTotalNs / 1000.0 / windowGameFrames`
  - `avgUsPerCall = deltaCalls == 0 ? 0 : deltaTotalNs / 1000.0 / deltaCalls`
- emit those values for `RunGF` and the selected sections,
- keep logging the existing global search counters as deltas in the same row.

Special handling for the first logged row:

- `ElapsedMillis` already starts at `0`; apply the same pattern to
  `WindowGameFrames`,
- for profiler sections, use deltas against an all-zero snapshot,
- document that the first row represents cumulative work from start to first log
  point.

### 6. Keep shutdown summary behavior intact

Files:

- `libs/s25main/ai/aijh/debug/AIRuntimeProfiler.cpp`

Changes:

- retain the current destructor summary and sorting,
- do not replace full-run totals with windowed data,
- optionally add `RunGF` naming polish so the shutdown table matches the CSV.

This preserves the existing workflow for one-shot profiling runs.

## CSV Schema Recommendation

Recommended stable column order:

1. `GameFrame`
2. `ElapsedMillis`
3. `WindowGameFrames`
4. `RunGF_AvgUsPerGF`
5. `RunGF_AvgUsPerCall`
6. `RunGF_Calls`
7. for each remaining selected section:
   - `<SectionName>_AvgUsPerGF`
   - `<SectionName>_AvgUsPerCall`
   - `<SectionName>_Calls`
8. `GlobalPositionSearchInvocations`
9. `GlobalPositionSearchCooldownSkips`

Why this order:

- global timeline context first,
- total `RunGF` signal next,
- then section breakdown,
- then legacy activity counters.

## Verification Plan

### Build validation

- build the AI-related target(s) affected by the change,
- verify no enum/table mismatch remains after adding `RunGF`,
- verify the reporter compiles cleanly against the new snapshot API.

### Runtime validation

Run one AI-enabled match with `RTTR_AI_PROFILE=1` and check:

1. the game still writes `ai_performance.csv`,
2. the header contains the new runtime span columns,
3. `RunGF_AvgUsPerGF` is non-zero for active AI players,
4. periodic sections such as `EvaluateCaptureRisks` or
   `CheckEconomicHotspots` show non-zero calls only in windows where they run,
5. the shutdown summary still prints and includes `RunGF`.

### Data sanity checks

For at least one log window, verify:

- `RunGF_Calls` is approximately equal to the number of `RunGF()` invocations in
  the window,
- `RunGF_AvgUsPerCall` is greater than or equal to any child section average in
  a comparable window,
- infrequent sections have low `Calls` but can still show high
  `AvgUsPerCall`,
- the sum of selected section `AvgUsPerGF` values stays meaningfully below or
  near `RunGF_AvgUsPerGF`, acknowledging unprofiled overhead and overlap rules.

## Non-Goals

- Do not add a third-party tracing framework in this change.
- Do not redesign the whole AI stats subsystem.
- Do not add new deep spans everywhere at once.

The first implementation should make the existing profiler observable during the
game, not broaden instrumentation indiscriminately.

## Follow-Up Candidates

After this lands and produces useful late-game data, likely follow-ups are:

- add CSV emission for selected deeper sections such as:
  - `ExecuteEventJobs`
  - `ExecuteConstructionJobs`
  - `ExecuteGlobalBuildJobs`
  - `ExecuteBuildJobs`
  - attrition subphases
- add optional `avg work per call` columns where work-unit counts are already
  tracked,
- add per-player identifiers to the CSV if multiple AI players write to the
  same stats directory,
- add a focused late-game benchmark or regression test around the hottest
  sections found.
