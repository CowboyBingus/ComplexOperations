# The extra large world's prototype, as copied

These files were written inside a HellForge loader. They were copied here byte for byte and are compiled only as parts
of `../co_xl.c`, which includes them in the loader's order. `scripts/build.sh` compiles none of them on its own.

Change `../co_shim.h`, `../co_shim.c` or `../co_xl_standin.h` before one of these files, and list every edit to them in
the last section.

## Where each file came from

| Files | Source | Commit |
|---|---|---|
| 29 files: every file here except the ones below | HellForge, branch `ds/mission-arcs`, `native/mod-loader/` | `0e90b26374173cb6ff743aac7fec3bfcc7d7062b` |
| `living_world*.c`, `living_world_defer_list.h`, `safe_read.c` (13 files) | HellForge, branch `ds/living-world`, `native/mod-loader/` | `b3de43d9259530f5e4a9ff2a787e26e49c9ea000` |
| The 27 other `living_world_*.c`, and the fork's later edits to `living_world.c`, `_defer.c`, `_density.c`, `_flat.c`, `_render.c` | the same folder's uncommitted work on top of `b3de43d9`, merged three ways with the edits below | none |
| `../overlay/hellforge_overlay_host.h` | HellForge, branch `ds/mission-arcs`, `native/overlay/` | `0e90b263` |
| `../sdk-fallback/hellforge_overlay.h` | HellForge, branch `ds/mission-arcs`, `native/mod-sdk/` | `0e90b263` |

The two overlay headers keep the places the tactical map's includes expect. `../mod-sdk/hellforge_overlay.h` is not a
copy: it passes the include on to the SDK's header, or to the fallback when the SDK checkout has none.

## How they are used

| Files | Use |
|---|---|
| 24 `.c` files named in `../co_xl.c` | Included there, in the loader's order. |
| `living_world_*.c`, `tacmap_*.c`, `tacmap.h`, `world_sites.h`, `living_world_defer_list.h` | Included by the files above. |
| `safe_read.c` | Included by `../co_shim.c`: the mod's guarded read. |
| `xl_span_rescue.c`, `xl_span_rescue_pins.h` | The mod's own, not copied. A full rpmalloc span that other threads freed blocks into goes back to its heap's partial list. `../co_xl.c` installs it after the streamer and calls its pass from the tick. `scripts/gen_span_rescue_pins.py` writes the pins. |
| `important_sites.h`, `route_sites.h` | Not compiled. They are tables of the generation block's move, which is HellForge's. |

## What is left out

- The loader's own level 11 install and the fork's edits to it. `../co_xl_standin.h` gives the names these files read.
- The loader's `PhysicsPools` with the fork's pool sizes, broadphase and navigation cells. `../../co_early.c` writes
  them beside HellForge's own, and `../co_xl.c` hooks the entity records' move from the same moment (`CoXlEarly`).
- The callers of `EntityRecordsWatch` and `StatusWatch`: the loader's watch thread. The first only notes a count.
  The second is called from the game thread's tick (`../co_xl.c`); the registry it installed is `../co_status.c`'s.
- The fork's edits to the loader's level install that the mod does without: markers 64 (the map keeps 16,
  `../co_markers.h`), the objective list of 48 with the result rows (the list stays at 30, `../co_objectives.c`), a
  different partner per extra mission (HellForge's one partner is repeated).

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
| `difficulty_world.c` | A mission's level init and the ship tell `../co_spawners.c` whether the mission in play is complex; the stream note's count of spawner positions past a group's places is that file's. |
| `difficulty_world.c` | Level init sets the stamp index's slots for the level (`../co_stamps.c`) and rebuilds the index when it was built as another level's. The index query's hook is in `../co_xl.c`. |
| `difficulty_world.c` | The install goes on, behind the chains, with `../co_pocket.c`: FixUnreachableAreas' pocket search, component list and reconnect for a complex level. |
| `difficulty_world.c` | The cluster pass sets its divisor through `../co_clusters.c`, whose stubs stand in front of HellForge's two counts; `g_clusterPer` is gone. |
| `difficulty_world.c` | Level init sets the city footprint list's places and the city op's location capacity for the level (`../co_city.c`). |
| `difficulty_world.c`, `difficulty_world_gates.c` | Sites HellForge's level 11 has a step at (`../co_beside.c`): the thinning runs in front of its merge stub, the mine list and the records' store keep its guards, and the city gates' pins accept its jumps. |
| `difficulty_world.c`, `difficulty_world_arcs.c` | The objective fill's list is made by `../co_objectives.c` from the selection: within the reported places, a mission's primaries as whole blocks. The arcs take such a list's blocks from it. `ObjectiveIdCap` moved there. |
| `difficulty_status.c` | The registry's stubs, list and hash are `../co_status.c`'s (seven sites, all or none, installed by `co_complex_start`); `StatusWatch` is called from the game thread's tick, moves the hash and notes skipped units. |
| `difficulty_world.c` | The mission in play sets where a loot container's location is looked up (`../co_locate.c`): the moved array on a complex mission. |
| `difficulty_world.c` | The HUD rows' text ids start at `CO_HUD_TEXT_FIRST` (`../co_xl_standin.h`); `../co_text.c` answers them behind the engine's lookup. |
| `living_world.c` | The tick's cost by section is logged by a research build only (`CO_RESEARCH`). |
| `living_world*.c`, `difficulty_world.c` | The living world is on without a switch and acts only while the mission in play is complex (`LwLive`; `LwLiveSet` follows the live page). The patrol rows are read, never written: `../co_patrols.c` switches the patrols off. |
| `living_world.c` | The nav leave fix's stub tests a flag; the walls' reach is -0 on any other mission; the walls' request count is research-only. |
| `living_world_defer.c` | The setup and sample passes are switched sites (`../co_switch.c`): the mod's path on a complex mission, the game's bytes on any other. They go in only with the density guards (`../co_density.c`). The four guards are installed by `../co_xl.c` (`CoXlGuards`). |
| `living_world_density.c` | `LwDensityOn` asks `co_density_on` instead of reading the guards' bytes. |
| `living_world.c` | `LwLiveSet` switches the frame-time levers with the mission's kind and logs one line at each change. The render phase timer's and the census's commands go to an empty `LwRmCmd`. |
| `living_world_zerobox.c` | The absent SpeedTree cells' box is a switched site of group `CO_SWITCH_XL` (`../co_switch.c`), on by default. Its jump is written once with the other threads stopped, listed before any is. |
| `living_world_navjob.c`, `living_world_vbox.c` | On by default. Their hooks pass straight to the game's code unless the mission is complex. Leaving one, the nav job returns to mode 0 with the game's vtable, and the vehicle boxes drop their copies. The nav job's completion sites are research only. |
| `living_world_corpse.c`, `living_world_aibudget.c` | Their leave clears the mod's own state only; the AI budget writes nothing then. |
| `living_world_bp.c` | Research only (`HELLFORGE_LW_BP=1`); its hooks act only on a complex mission. |
| `living_world_defer.c` | Every research install of the levers and instruments is under `CO_RESEARCH`. |
| `living_world_render.c` | The render thread's test delay is research only. |
| `living_world_navjob.c`, `_vbox.c`, `_zerobox.c` | The exe's base is the host's `helldivers2.exe`. |
| `living_world_records.c` | The map's slots are read (HellForge's 0x10000 too). The multiplier is the scatter on a complex level and the game's 1 on any other; the pin is the constructor's multiplier, not its capacity. |

What the build does instead of an edit:

- `../co_xl.c` turns `-Wunused-function` off around the includes, for the six functions named above.
- `../co_xl.c` defines `HF_LUA_API` as empty, so three research functions are not exported from the mod's DLL.
