---
title: "Economy production timings"
source: "Runtime code inspection"
last_checked: "2026-05-04"
---

# Economy production timings

This note documents the runtime periods around ordinary production buildings:

- when an input ware is accepted by a building,
- when that ware is consumed,
- how long the production cycle takes,
- when the produced ware appears at the building flag,
- and when the next input can be consumed.

All numbers are in game frames (GF). At normal game speed, one GF is 50 ms.

## Code path

For normal indoor producers, delivered input wares are stored in
`nobUsual::numWares`. They are not consumed on arrival. Consumption happens when
the worker enters `nofWorkman::StartWorking()`:

1. `nobUsual::AddWare()` increments stored input count and wakes the worker.
2. `nofBuildingWorker::TryToWork()` starts the first wait period
   (`JOB_CONSTS[job].wait1_length`) when production is enabled and input is
   available.
3. `nofWorkman::StartWorking()` begins the work event and calls
   `nobUsual::ConsumeWares()`.
4. `nofWorkman::HandleStateWork()` waits `wait2_length` after work finishes.
5. `nofWorkman::HandleStateWaiting2()` creates the carried output ware.
6. The worker walks out to the flag and `nofBuildingWorker::WorkingReady()`
   places the ware there.
7. The worker walks back into the building and calls `TryToWork()` again.

Relevant implementation files:

- `libs/s25main/buildings/nobUsual.cpp`
- `libs/s25main/figures/nofBuildingWorker.cpp`
- `libs/s25main/figures/nofWorkman.cpp`
- `libs/s25main/gameData/JobConsts.cpp`
- `libs/s25main/gameData/BuildingConsts.h`
- `libs/s25main/nodeObjs/noMovable.cpp`

## Walking assumptions

The worker uses `StartWalking(..., 20)` for one building-door/flag step.
`noMovable::StartMoving()` adjusts this for slope:

- flat or downhill: `20 GF`,
- altitude difference `+1`: `30 GF`,
- altitude difference `+2` or `+3`: `40 GF`,
- altitude difference `+4` or `+5`: `60 GF`.

The timing table below assumes the usual flat one-step walk:

- `walkOut = 20 GF`,
- `walkIn = 20 GF`.

If the door/flag step is uphill, add the slope difference to the affected
walk. For example, if walking out costs `40 GF` instead of `20 GF`, add `20 GF`
to "consume -> product on flag".

## Normal indoor producer formula

For a fully supplied normal indoor producer:

- `accepted input -> consumed input = wait1_length`, if the worker was idle and
  waiting for input.
- `consumed input -> output on flag = work_length + wait2_length + walkOut`.
- `output on flag -> next consumed input = walkIn + wait1_length`.
- `consumed input -> next consumed input =
  work_length + wait2_length + walkOut + walkIn + wait1_length`.

If the worker already has enough stored inputs and is already inside waiting or
working, a later accepted ware may sit in storage for more than one cycle before
being consumed.

## Timing table

| Building | Worker job | Inputs consumed per cycle | Output | Accepted -> consumed if idle | Consumed -> output on flag | Output on flag -> next consumed | Consumed -> next consumed |
|---|---|---|---|---:|---:|---:|---:|
| Sawmill | Carpenter | Wood | Boards | 96 | 504 | 116 | 620 |
| Mill | Miller | Grain | Flour | 95 | 495 | 115 | 610 |
| Bakery | Baker | Flour + Water | Bread | 94 | 495 | 114 | 609 |
| Slaughterhouse | Butcher | Ham/Pig ware in code | Meat | 80 | 503 | 100 | 603 |
| Brewery | Brewer | Grain + Water | Beer | 93 | 555 | 113 | 668 |
| Pig Farm | Pig breeder | Grain + Water | Ham/Pig ware in code | 160 | 415 | 180 | 595 |
| Coal Mine | Miner | One food item | Coal | 558 | 608 | 578 | 1186 |
| Iron Mine | Miner | One food item | Iron ore | 558 | 608 | 578 | 1186 |
| Gold Mine | Miner | One food item | Gold ore | 558 | 608 | 578 | 1186 |
| Granite Mine | Miner | One food item | Granite/Stones | 558 | 608 | 578 | 1186 |
| Ironsmelter | Iron founder | Iron ore + Coal | Iron | 160 | 975 | 180 | 1155 |
| Mint | Minter | Gold + Coal | Coins | 170 | 1075 | 190 | 1265 |
| Armory | Armorer | Iron + Coal | Sword/Shield alternation | 170 | 965 | 190 | 1155 |
| Metalworks | Metalworker | Iron + Boards | Selected tool | 400 | 875 | 420 | 1295 |
| Donkey Breeder | Donkey breeder | Grain + Water | Pack donkey figure | 278 | 575 to donkey creation, no ware flag output | 278 after donkey creation | 853 |
| Winery | Vintner | Grapes | Wine | 95 | 495 | 115 | 610 |
| Temple | Temple servant | Wine + Meat + Bread | Gold | 95 | 590 | 115 | 705 |

## Special cases

### Mines

Mines accept fish, meat, and bread, but `BLD_WORK_DESC` sets
`useOneWareEach = false`, so one production cycle consumes only one food item.
`nobUsual::ConsumeWares()` chooses the stored input type with the highest count.

### Armory

The armory alternates sword and shield production. With the
`HALF_COST_MIL_EQUIP` addon enabled, the shield half of the alternation can work
without consuming a new iron + coal pair.

### Metalworks

The metalworker consumes iron + boards only after choosing a tool to produce. If
tool priorities/orders allow no tool, `StartWorking()` can abort and no input is
consumed.

### Donkey Breeder

The donkey breeder follows `nofWorkman` timing for input consumption, but
`ProduceWare()` returns no ware. `WorkFinished()` creates a `PackDonkey` figure
after the post-work wait. There is no output ware placed at the flag.

### Outdoor and resource-field workers

The following buildings are not fixed indoor input-output cycles:

- Woodcutter,
- Fishery,
- Quarry,
- Forester,
- Farm,
- Hunter,
- Charburner,
- Vineyard.

They walk to a selected outside work point and back, so the total cycle depends
on path length and terrain. Their fixed pre-work wait and work durations still
come from `JOB_CONSTS`, but walking dominates and varies.

Charburner and Vineyard also have input-consuming planting/filling phases:

- Charburner consumes wood/grain in `nofCharburner::WalkingStarted()` when it
  goes out to plant or feed a charcoal pile. Harvesting coal from a ready pile
  does not consume input.
- Vineyard consumes wood/water in `nofWinegrower::WalkingStarted()` when it goes
  out to plant grapes. Harvesting grapes does not consume input.

## Job constants

These are the raw `JOB_CONSTS` values used by the formulas:

| Job | work_length | wait1_length | wait2_length |
|---|---:|---:|---:|
| Helper | 385 | 190 | 5 |
| Woodchopper | 148 | 789 | 5 |
| Fisher | 129 | 825 | 5 |
| Ranger/Forester | 66 | 304 | 5 |
| Carpenter | 479 | 96 | 5 |
| Stonemason | 129 | 825 | 5 |
| Huntsman | 0 | 300 | 5 |
| Farmer | 117 | 106 | 5 |
| Miller | 470 | 95 | 5 |
| Baker | 470 | 94 | 5 |
| Butcher | 478 | 80 | 5 |
| Miner | 583 | 558 | 5 |
| Brewer | 530 | 93 | 5 |
| Pig breeder | 390 | 160 | 5 |
| Donkey breeder | 370 | 278 | 205 |
| Iron founder | 950 | 160 | 5 |
| Minter | 1050 | 170 | 5 |
| Metalworker | 850 | 400 | 5 |
| Armorer | 940 | 170 | 5 |
| Shipwright | 1250 | 100 | 5 |
| Charburner | 117 | 106 | 5 |
| Winegrower | 117 | 106 | 5 |
| Vintner | 470 | 95 | 5 |
| Temple Servant | 470 | 95 | 5 |
