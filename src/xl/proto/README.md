# The extra large world's prototype, as copied

These files were written inside a HellForge loader. They were copied here byte for byte and are compiled only as parts
of `../co_xl.c`, which includes them in the loader's order. `scripts/build.sh` compiles none of them on its own.

Change `../co_shim.h`, `../co_shim.c` or `../co_xl_standin.h` before one of these files, and list every edit to them in
the last section.

## Where each file came from

| Files | Source | Commit |
|---|---|---|
| 39 files: every file here except the two below | HellForge, branch `ds/mission-arcs`, `native/mod-loader/` | `0e90b26374173cb6ff743aac7fec3bfcc7d7062b` |
| `safe_read.c`, `living_world_mover.c` | HellForge, branch `ds/living-world`, `native/mod-loader/` | `7b9a836039fe6771eabaa53385214633c6a5a668` (both last changed in `acf4a4da`) |
| `../overlay/hellforge_overlay_host.h` | HellForge, branch `ds/mission-arcs`, `native/overlay/` | `0e90b263` |
| `../sdk-fallback/hellforge_overlay.h` | HellForge, branch `ds/mission-arcs`, `native/mod-sdk/` | `0e90b263` |

The two overlay headers keep the places the tactical map's includes expect. `../mod-sdk/hellforge_overlay.h` is not a
copy: it passes the include on to the SDK's header, or to the fallback when the SDK checkout has none.

## How they are used

| Files | Use |
|---|---|
| 24 `.c` files named in `../co_xl.c` | Included there, in the loader's order. |
| `living_world_*.c` (but `living_world_mover.c`), `tacmap_*.c`, `tacmap.h`, `world_sites.h`, `living_world_defer_list.h` | Included by the files above. |
| `living_world_mover.c` | Included by `../co_xl.c` after `living_world.c`. In its own branch `living_world.c` includes it. |
| `safe_read.c` | Included by `../co_shim.c`: the mod's guarded read. |
| `important_sites.h`, `route_sites.h` | Not compiled. They are tables of the generation block's move, which is HellForge's. |

## What is left out

- The loader's own level 11 install and the fork's edits to it. `../co_xl_standin.h` gives the names these files read.
- The density rules' calls into `living_world_mover.c` (`LwMoverRoom`, `LwMoverPending`, `LwMoverSent`, `LwMoverNote`).
  They are in a later `living_world_density.c` that was not copied.
- The loader's `PhysicsPools` with the fork's pool sizes, broadphase and navigation cells. `../../co_early.c` writes
  them beside HellForge's own, and `../co_xl.c` hooks the entity records' move from the same moment (`CoXlEarly`).
- The callers of `EntityRecordsWatch` and `StatusWatch`: the loader's watch thread. The first only notes a count.
  The second installs the status volumes' larger unit registry, whose five stubs leave two uses of the game's own list
  as they are (an append at 0x13CC3F6, a read at 0x13CC03F, in functions no call in the image reaches): it stays
  uninstalled until those are covered.
- The fork's edits to the loader's level install that are neither here nor in HellForge: the city footprint list and
  location reserve, the pocket search, the clusters' divisor, spawners 1024, markers 64, the objective list of 48
  with the result rows, a partner per extra mission, the location site 0xF73E79.

## Edits to the copied files

| Files | Edit |
|---|---|
| `difficulty_world_terrain.c` | The stamp runners leave one worker of the frame's queue free (`StampRunnerCap`). |
| `difficulty_world.c`, `_arcs.c`, `_macro.c`, `_terrain.c`, `difficulty_stream_window.c` | The generation block's offsets and counts are HellForge's (`../co_xl_standin.h` over `co_gen_at`, `co_gen_places`): no literal offset of a moved array, tables sized `CO_LOCATIONS_MAX`, the objective list bounded by its reported places. |
| `difficulty_world.c` | The world's readers are pointed by `../co_pages.c` at two pages (`WorldValues` is its generation page). The level-init stub passes the preview's record; `WorldLevel` changes play's page for a mission only. |
| `difficulty_stream.c`, `difficulty_world_move.c`, `_destroy.c`, `_terrain.c`, `difficulty_net_patience.c`, `living_world_defer.c` | Play code asks the mission in play (`g_world.live`, `g_worldLevelLive`, `liveLevels`), not the level set up last. |
| `difficulty_world.c`, `difficulty_world_towns.c` | A level init drops what the last generation left: record addresses, open nature windows, the city shift, an armed towns run, the HUD order, the height copy's level. |
| `living_world.c`, `living_world_intent.c` | The per-mission resets count missions (`g_lwMission`) instead of comparing level addresses. |
| `difficulty_world.c` | Level init asks `co_generation_decision` where it asked for difficulty 11; the complex level's radii stub (`../co_normal.c`) goes in behind HellForge's. |
| `living_world.c`, `difficulty_stream.c`, `difficulty_world_terrain.c` | `hellforge_lw_test` and `hellforge_research_stream_observer` are not exported. |
| `difficulty_world.c` | The install ends with `../co_chain.c`: the mod's stubs in front of HellForge's keyed generator steps. |
| `difficulty_world.c` | A level init whose decision is "leave" logs one line and sets up a normal level. |
| `difficulty_world.c` | `WorldShip` is called from the ship's navigation step (`../co_nav.c`), which the mod chains in front of HellForge's. |
| `difficulty_world.c` | Level init sets the distance row for the level (`../co_rows.c`); the city zone's ratio is that file's. |
| `difficulty_world.c` | The HUD's three stubs come from `../co_normal.c`: each runs the game's own bytes unless the mission in play is complex, and the row state hook passes a normal mission through. |
| `difficulty_world.c`, `difficulty_world_arcs.c` | Level init records whether the level is complex by its address (`LevelComplex`); the objective fill and the mission arcs ask it. The arcs' `HELLFORGE_XL_ARCS` switch is gone. |
| `difficulty_world.c` | Level init sets a weld block's places for the level (`../co_weld.c`). |
| `difficulty_world.c` | Level init sets the stamp index's slots for the level (`../co_stamps.c`) and rebuilds the index when it was built as another level's. The index query's hook is in `../co_xl.c`. |
| `difficulty_world.c` | The install goes on, behind the chains, with `../co_pocket.c`: FixUnreachableAreas' pocket search, component list and reconnect for a complex level. |
| `difficulty_world.c`, `difficulty_world_gates.c` | Sites HellForge's level 11 has a step at (`../co_beside.c`): the thinning runs in front of its merge stub, the mine list and the records' store keep its guards, and the city gates' pins accept its jumps. |
| `difficulty_world.c`, `difficulty_world_arcs.c` | The objective fill's list is made by `../co_objectives.c` from the selection: within the reported places, a mission's primaries as whole blocks. The arcs take such a list's blocks from it. `ObjectiveIdCap` moved there. |

What the build does instead of an edit:

- `../co_xl.c` turns `-Wunused-function` off around the includes, for the seven functions named above.
- `../co_xl.c` defines `HF_LUA_API` as empty, so three research functions are not exported from the mod's DLL.
