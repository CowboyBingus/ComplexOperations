# Complex Operations

A standalone HellForge code mod for eight-player sessions. Two parts are in: eight hellpods on the ship, and eight
slots in the game's per-player component managers. A session still takes four players.

It runs on game build 25480438 only. On any other build it logs the reason and changes nothing.

## What it does

| Part | Effect |
|---|---|
| Pod manager | The game's ship hellpod manager and the ship's pod table hold eight pods instead of four. |
| New pods | Four more launchers of the bridge's level become pods: hellpod5 to hellpod8. |
| Launch | The launch cutscene fires pod n + 4 together with pod n. |
| Briefing camera | Pods 5 to 8 get a briefing camera of their own. |
| Player managers | Seven per-player component managers hold eight players instead of four (see below). |

The mod needs the bridge with the eight-pod bay: level data that places the four launchers (with the names
hellpod5 to hellpod8), four briefing cameras and the bay's lamps. Without that data it leaves a normal ship with four
pods and says so in its log.

Every player in the session needs the mod.

## Player managers

The game keeps one record for each player in seven managers: player, score, customization, kill streak, ship
inventory, ship customization and ship modules. Each has four slots. The mod builds all seven in its own memory with
eight slots and changes the 1,053 code sites that address them.

This alone lets no fifth player join: the session's own lists still hold four. It is the base the later parts need.

## State

- Tested with one player: all eight pods are entered on foot, launch, and are there again after the return.
- Not tested: two or more players in one session, and the launch cutscene for four or more.
- The launch sound for a lone player in pods 5 to 8 is hooked; the mod logs once when it answers for such a pod.

## Build

```text
scripts/build.sh [--test] [--package] [--hellforge <HellForge checkout>] [--zig <zig>]
```

The script builds `complex_operations.dll` beside `hellforge-mod.json`. It takes the mod SDK from a HellForge checkout
(`HELLFORGE_OPEN`, else `~/HellForge`) and Zig from `HELLFORGE_ZIG`, else `~/.local/share/zig-native/zig`.

`--package` writes `build/package/complex-operations-<version>.zip` with the four files a player installs.

`--test` also builds and runs `tests/*_test.c` as Windows programs under Wine (`WINE`, else Proton Experimental's).
The tests run the mod against a stand-in game image: no game is needed.

## Install

The mod needs a HellForge with mod SDK 17 or newer. Older ones cannot build the level data from the recipe and refuse
the mod.

Put these four files in one folder inside HellForge's mods folder:

- `hellforge-mod.json`
- `complex_operations.dll`
- `hellforge-recipe.json`
- `hellforge-recipe.literals`

HellForge builds the bay's level data from the recipe and the player's own game files before a session, into the
folder's `data/`. The recipe copies every unchanged value from the player's game. It ships only the values the mod changed (a moved
vertex's new coordinate, for example) and the mod's own new data. Without the level data the mod leaves four pods.

The recipe is written by the bay's build tools (`bay8_recipe.py hellforge`), which are not in this folder.

## Source

| File | Holds |
|---|---|
| `src/co_mod.c` | The entry point: the build check and the game-loaded event. |
| `src/co_pods.c` | The eight pods: memory, code sites, hooks. |
| `src/co_pod_sites.h` | The code sites that address the manager and the pod table. Generated; do not edit. |
| `src/co_managers.c` | The seven player managers: memory and code sites. |
| `src/co_manager_sites.h` | Their code sites. Generated; do not edit. |
| `scripts/sites/` | The reviewed site lists of the seven managers. |
| `scripts/gen_manager_sites.py` | Writes `co_manager_sites.h` from those lists and a dump of the game's code. |
| `hellforge-recipe.json`, `.literals` | The bay's level, geometry and collision data as a HellForge recipe. |
| `scripts/pod_sites.tsv` | The reviewed site list of the pod manager: one row for each instruction. |
| `scripts/gen_pod_sites.py` | Writes `co_pod_sites.h` from the site list and a dump of the game's code. |
| `tests/pods_test.c` | The refusals: another build, changed code, a failed write, a failed hook. |
| `tests/managers_test.c` | The same refusals for the player managers. |
| `tests/harness/` | The stand-in loader and game image the tests run against. |

## How it stays safe

- Every code site is compared with the bytes it was reviewed with before anything is written. One mismatch leaves the
  game as it is.
- Rows for the new pods are added only when the level's four units carry their pod names.
- A write that fails part-way sets every site written before it back to the game's bytes.
- A failed step is logged in plain words, for example "four pods as before".

## Log

The mod's lines are in the session's `game/mod-cowboybingus.complex-operations.log` and start with `hellpods:` or
`players:`.

| Line | Meaning |
|---|---|
| `the pod manager and the ship's pod table have 8 slots` | The code is in place. |
| `7 component managers have 8 slots` | The player managers are in place. |
| `the bridge is not the one with the eight-pod bay` | The level data is missing: four pods. |
| `... is not the reviewed one` | The game's code differs from the reviewed build: that part is off. |
