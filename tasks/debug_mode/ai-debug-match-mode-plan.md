# AI Debug Match Mode

## Summary

Add a host-only `Debug mode` checkbox to new-match and savegame lobbies. The choice applies only to the current lobby session: it defaults off, is not serialized, and does not alter savegame, replay, or network formats.

In Debug mode, JH AI state remains inspectable but autonomous command generation stops. Commands are allowed only as part of a human-triggered workflow: build one selected building, try one land attack, or build an alternative road for a clicked AI-owned flag.

## Implementation Steps

### 1. Add Lobby-Session Runtime State

- Add a non-serialized `bool aiDebugMode_ = false` field with getter/setter to `GameLobby`.
- Add a host-editable `Debug mode` checkbox in `dskGameLobby`, available for both new games and loaded savegames.
- Cancel an active start countdown when the checkbox changes.
- Do not place the setting in `GlobalGameSettings`; loaded saves and replay playback therefore start with Debug mode off.
- Copy the value into a new runtime-only `Game::aiDebugMode_` field in `GameClient::StartGame()` before releasing the lobby.
- Add `Game::IsAIDebugMode() const`. Give constructor parameters a default of `false` so tests and headless tools retain current behavior.
- Pass the runtime flag through `GameClient::CreateAIPlayer()` and `AIFactory::Create()` into `AIPlayerJH`.

### 2. Add a Mutable Debug Action Interface

Keep the existing `AIDebugView` read-only. Add a separate `AIDebugController`, implemented by `AIPlayerJH`, with:

```cpp
enum class DebugActionResult {
    Accepted, Issued, Busy, Disabled, InvalidTarget, NoCandidate
};

enum class DebugBuildState { Idle, Running, Succeeded, Failed };

virtual DebugActionResult RequestDebugBuild(BuildingType type) = 0;
virtual DebugActionResult TryDebugAttack() = 0;
virtual DebugActionResult TryDebugAlternativeRoad(MapPoint flagPt) = 0;
virtual DebugBuildState GetDebugBuildState() const = 0;
```

- Add a narrow `GameClient::GetAIDebugController(playerId)` lookup instead of exposing general mutable AI access.
- Return `Disabled` unless the match is in Debug mode.
- Use `NoCandidate` when normal heuristics cannot find a valid attack, placement, or shortcut.
- Retain the last build terminal state until the next build request so the UI can report completion or failure.

### 3. Stop Autonomous AI Commands

Refactor `AIPlayerJH::RunGF()` into normal and Debug-mode paths.

- Preserve command-free maintenance required by overlays and manual actions: frame bookkeeping, cache pruning, statistics, profiling, navigation cleanup, building-quality refresh, and planner snapshots.
- Preserve NWF construction progression needed by an explicitly requested build workflow.
- Skip autonomous initialization commands, event reactions, normal construction jobs, policy updates, distribution changes, defeat handling, expeditions, attacks, sea attacks, troop limiting, and chat.
- Drain ignored AI events each cycle so paused AIs do not accumulate stale work.
- Process only construction events belonging to explicit debug workflows, including road-completion postprocessing such as interior flag placement.

Add defense-in-depth at `AICommandSink`:

- Normal games continue accepting commands unconditionally.
- Debug-mode sinks reject commands by default.
- `AIPlayerJH` opens a scoped authorization only while executing a human-requested action or advancing its authorized build workflow.
- Keep chat outside the authorized path.

### 4. Implement Manual Building Workflow

Extend `AIConstruction` and `BuildJob` with a `DebugRequested` purpose.

- Store one active debug-owned `BuildJob` per AI separately from normal planning queues.
- `RequestDebugBuild(type)` rejects a second request with `Busy` while one is active.
- Bypass wanted-building-count checks for the selected type.
- Reuse normal placement search, buildability checks, site placement, primary road connection, secondary-road heuristics, waterway handling, and required road-completion processing.
- Advance the debug job automatically during Debug-mode ticks until it succeeds or fails; the user does not need to click for each state-machine step.
- Do not enqueue economy-chain follow-ups, retries, or unrelated construction jobs.
- Tag roads emitted by the debug build so only their completion events may generate authorized postprocessing commands.

### 5. Implement Attack and Alternative-Road Actions

- Implement `TryDebugAttack()` by invoking the existing configured land-target selector exactly once inside an authorized command scope.
- Report `Issued` only when the normal combat controller emits an attack command; report `NoCandidate` otherwise.
- Keep sea attacks out of the first version.
- Implement `TryDebugAlternativeRoad(flagPt)` using the existing shortcut-only `AIConstruction::BuildAlternativeRoad()` heuristic.
- Validate that the point still contains a flag owned by the selected AI.
- Tag an issued road as debug-owned so its completion postprocessing remains authorized.

### 6. Wire the In-Game UI

- In `iwMainMenu`, expose the host-only AI Debug window when runtime Debug mode is active.
- Preserve current ordinary-game behavior: debug builds may expose the window as today, and release builds may expose it through the existing `AI_DEBUG_WINDOW` addon.
- In `iwAIDebug`, show action controls only during runtime Debug mode:
  - building-type selector
  - `Build` button
  - `Try attack` button
  - concise status text for accepted, busy, failed, issued, and no-candidate results
- Reuse the existing building selector where possible, but keep it visible for manual builds independently of overlay selection.
- In `dskGameInterface`, add an `iwAction` Debug tab when the host clicks an AI-owned flag during runtime Debug mode.
- Put an alternative-road button in that tab. Use `iwAction`'s pinned `selectedPt`, not the current mouse-hover point.

### 7. Document the Mode

Add `docs/ai/debug-mode.md` and link it from `docs/README.md`.

- Explain lobby-only lifetime and default-off behavior.
- Distinguish runtime Debug mode from the existing AI Debug window addon.
- Document paused autonomous behavior and the three manual actions.
- State that one building workflow may run per AI and that manually authorized workflows can emit several related commands.

## Test Plan

- Verify the lobby checkbox defaults off for new and loaded games, is host-only, and cancels countdown changes.
- Verify savegame and replay serialization formats remain unchanged.
- Run a Debug-mode JH AI across normal frames and assert that no autonomous game commands or chat appear while read-only state refreshes.
- Verify a forced building request works when wanted count is zero, emits only its own site and road workflow commands, reaches a terminal state, and rejects concurrent requests.
- Verify ignored AI events are drained and unrelated road-completion events emit no commands.
- Verify alternative-road requests reject missing or foreign flags and emit a road command for a valid shortcut.
- Verify attack requests emit the normal land-attack command when a target exists and no command otherwise.
- Add UI coverage for checkbox availability, Debug-window action visibility, and the clicked AI-flag Debug tab.
- Run focused Boost.Test suites, then `ctest --test-dir build --output-on-failure`.

## Assumptions

- Debug mode controls JH AI players. Dummy AI remains unchanged because it does not generate gameplay commands.
- The host owns AI execution, so the runtime flag stays local; other clients receive ordinary synchronized gameplay commands.
- Alternative-road requests use the normal shortcut-only policy rather than forcing any valid loop.
- A human-triggered building workflow authorizes its required follow-up road commands, but no broader AI planning.
