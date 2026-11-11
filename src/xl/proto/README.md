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
- The callers of `WorldShip`, `EntityRecordsWatch` and `StatusWatch`: the loader's level change and watch thread.

## Edits to the copied files

| Files | Edit |
|---|---|
| `difficulty_world_terrain.c` | The stamp runners leave one worker of the frame's queue free (`StampRunnerCap`). |
| `difficulty_world.c`, `_arcs.c`, `_macro.c`, `_terrain.c`, `difficulty_stream_window.c` | The generation block's offsets and counts are HellForge's (`../co_xl_standin.h` over `co_gen_at`, `co_gen_places`): no literal offset of a moved array, tables sized `CO_LOCATIONS_MAX`, the objective list bounded by its reported places. |

What the build does instead of an edit:

- `../co_xl.c` turns `-Wunused-function` off around the includes, for the seven functions named above.
- `../co_xl.c` defines `HF_LUA_API` as empty, so three research functions are not exported from the mod's DLL.
