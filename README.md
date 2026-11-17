# Complex Operations

A standalone HellForge code mod for eight-player sessions: eight hellpods on the ship, eight slots in the game's
per-player managers and session lists. The default build still takes four players; `--players 8` builds one that
takes eight (see Build).

It runs on game build 25480438 only. On any other build it logs the reason and changes nothing.

## What it does

| Part | Effect |
|---|---|
| Pod manager | The game's ship hellpod manager and the ship's pod table hold eight pods instead of four. |
| New pods | Four more launchers of the bridge's level become pods: hellpod5 to hellpod8. |
| Launch | The launch cutscene fires pod n + 4 together with pod n. |
| Briefing camera | Pods 5 to 8 get a briefing camera of their own. |
| Player managers | Seven per-player component managers hold eight players instead of four (see below). |
| Enemy guards | Three of the game's unchecked writes refuse instead of crashing when many enemies are alive: a full squad-slot table, more than 64 kinds of enemy in the spawn budget, and a nearly full ray queue. The AI job gate for new spawns rises from 448 to 1024. The 70-enemy room, the breach release and the despawner count native enemies only, so living world squads leave natives their vanilla room. |

The mod needs the bridge with the eight-pod bay: level data that places the four launchers (with the names
hellpod5 to hellpod8), four briefing cameras and the bay's lamps. Without that data it leaves a normal ship with four
pods and says so in its log.

Every player in the session needs the mod.

## Player managers

The game keeps one record for each player in seven managers: player, score, customization, kill streak, ship
inventory, ship customization and ship modules. Each has four slots. The mod builds all seven in its own memory with
eight slots and changes the 1,053 code sites that address them.

This alone lets no fifth player join: the session's own lists still hold four. It is the base the later parts need.

## Session lists

The session keeps its peers in arrays of four, packed in one object. The lists that will hold eight stay in place.
The six arrays behind them move to the mod's memory: index flags, party records, ready peers, a third peer list,
peer values and hostability. 281 code sites get the new distance; 20 of them through a short stub, because the
instruction has no room for it.

With the room made, the lists count to eight: 120 sites that loop, compare or clear for four entries do so for eight.
In 17 places the game's compiler wrote the code out for exactly four entries; there the mod adds the other four, with
code of its own beside the game's (14) or a hook of the function (3).

Other tables of the game stay at four entries and are indexed by a player index with no check. Among them are the
galactic map's cursors, the game state's player rows, the loadout screen's cards, the score slots and the mission
summary's rows. 70 guards cover the accesses to them: an index of four or more skips the access, and for 0 to 3 nothing
changes. 61 guards are code beside the game's; 9 are a compare changed in place. The guards go in and out together
with the counts.

Some message handlers copy a list from the wire into a buffer of fixed size and trust the count. Nine guards keep
such lists inside their buffers: the handler ignores a message whose list is too long, and two senders stop at what
the receiver holds. Every build has them.

A build for eight players also lets a client's player index 4 to 7 count as valid, opens the join gate to eight, and
makes the SOS beacon count to eight.

Limits that remain:

- The hostability scores stay at four entries. Their message carries five at most, so peers 5 to 8 get no score.
- A guarded table has no entry for players 5 to 8: no map cursor, no loadout card, no row in the mission summary.

## Engine

The engine sends a session's peers as a count and a list. The sender writes the count as a whole byte; the readers
take three bits of it, so eight peers arrive as none. Five sites in `helldivers2.exe` make the readers take four
bits. With up to seven peers the message is unchanged.

## State

- Tested with one player: all eight pods are entered on foot, launch, and are there again after the return; missions
  and returns work with the lists counting to eight. A mission from a new pod after a completed mission's summary
  works; the mission was ended through the game's own handler, not by flying an extraction.
- Tested with two players (two game instances on one PC, build for four): the join, both players on the ship, a
  seat in a new pod seen by the other player.
- Not tested: a build for eight players in a game, a player with index 4 to 7, a launch with two or more players.
- The launch sound for a lone player in pods 5 to 8 is hooked; the mod logs once when it answers for such a pod.

## Build

```text
scripts/build.sh [--test] [--package] [--players 4|8] [--test-first-slot <1-7>] [--hellforge <checkout>] [--zig <zig>]
```

`--players` sets how many players a session takes (default 4). The DLL and the tests are built for that count.

`--test-first-slot` is a test aid for one PC with two game instances. As host, the mod gives joining peers slots
and player indexes from that number on, so one joiner reaches the code for players 5 to 8. It needs `--players 8`,
`--package` refuses it, and the log says it is a test build.

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
| `src/co_engine.c` | The engine's peer-count readers. |
| `src/co_session.c` | The session lists: the extension block, code sites and stubs. |
| `src/co_session_sites.h` | Their code sites. Generated; do not edit. |
| `src/co_density.c` | The enemy guards: code sites, stubs, the ray guard and the living world's unit count. |
| `tests/density_test.c` | The enemy guards' sites, stub jumps, unit count, ray refusals and failed writes. |
| `scripts/gen_session_sites.py` | Writes `co_session_sites.h` from the site list and a dump of the game's code. |
| `tests/managers_test.c` | The same refusals for the player managers. |
| `tests/session_test.c` | The same for the session lists, and that sites and stubs lead to the new place. |
| `src/co_counts.c` | The session lists' counts: sites, code caves, the hooks for entries 4 to 7. The guards too. |
| `src/co_count_sites.h` | Their code sites and the caves' code. Generated; do not edit. |
| `scripts/sites/count_sites.tsv` | The reviewed count sites: one row for each instruction. |
| `scripts/gen_count_sites.py` | Writes `co_count_sites.h`; assembles the caves with GNU `as`. Holds the guards. |
| `tests/counts_test.c` | The counts and guards: written behind the move, taken back on a failed write, entries 4 to 7. |
| `tests/harness/` | The stand-in loader and game image the tests run against. |

## How it stays safe

- Every code site is compared with the bytes it was reviewed with before anything is written. One mismatch leaves the
  game as it is.
- Rows for the new pods are added only when the level's four units carry their pod names.
- A write that fails part-way sets every site written before it back to the game's bytes.
- A failed step is logged in plain words, for example "four pods as before".

## Log

The mod's lines are in the session's `game/mod-cowboybingus.complex-operations.log` and start with `hellpods:`,
`players:`, `session:` or `engine:`.

| Line | Meaning |
|---|---|
| `the pod manager and the ship's pod table have 8 slots` | The code is in place. |
| `7 component managers have 8 slots` | The player managers are in place. |
| `7 arrays of the session lists moved` | The session lists' arrays are in the mod's memory. |
| `the session lists count to eight` | The lists' code counts to eight; the line says how many players get in. |
| `... guards keep the lists in messages inside the game's buffers` | The message guards are in place. |
| `a message with a list of ... entries was ignored` | A peer sent a longer list than the handler holds; once a handler. |
| `player indexes 4 to 7 are valid` | A build for eight players. |
| `TEST BUILD, not for play` | Built with `--test-first-slot`. |
| `the session lists count to four as before` | The count change is off; the reason stands before it. |
| `the bridge is not the one with the eight-pod bay` | The level data is missing: four pods. |
| `... is not the reviewed one` | The game's code differs from the reviewed build: that part is off. |
