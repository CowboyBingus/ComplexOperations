/* Read-only views of what a terrain's materials hang on, for the research log of crash exe+0x4F5037: a render
 * terrain's first resolve read a material variable block that was no longer one. Nothing here writes to the game or
 * calls it; every address is read through the caller's guarded read (0 = read). Offsets, exe build of the live dump:
 * - the material cache [world+0x2301F8]: 24-byte slots at +0x98 (key, material, refcount +0x10, generation +0x14),
 *   slot count +0xA8 (0x1DE198..0x1DE204); a terrain's keys are 0x..FFBAAB00000000 | dword [template+0x38] (0x2F450C);
 * - a material (clone 0x4F2DD0): handle +4, the template's block +0x10 and the template's id +0x38, both copied;
 * - a block: entries +0x48 (20 bytes each), count +0x50, data +0x58 (the faulting loop 0x4F4FC0..0x4F5049);
 * - a terrain (0x2F4400): lists at +0xA60 and +0xA78: count +0, 16-byte entries (name, material +8) at +8. */
#ifndef CO_TERRAINLOG_H
#define CO_TERRAINLOG_H

#include <stddef.h>
#include <stdint.h>

typedef int (*CoTlRead)(uintptr_t at, void* out, size_t size);

#define CO_TL_CACHE_SLOTS (1u << 18) /* a slot count above this is taken as a misread */
#define CO_TL_LIST_MOST 64u

typedef struct CoTlEntry { uint64_t key, material, block, id; int32_t refs; uint32_t generation, handle; } CoTlEntry;
typedef struct CoTlMaterial { uint64_t material, block, entries, data; uint32_t name, handle, count; } CoTlMaterial;
typedef struct CoTlTerrain { uint64_t handle, heightmap, material; } CoTlTerrain;

/* 1 for a key any terrain-material holder makes (bits 32..55 are 0xFFBAAB; the top byte is the holder's flag). */
int co_tl_terrain_key(uint64_t key);
/* The material's own fields into `e` (handle, block, id); 0 when it does not read. */
int co_tl_material(CoTlRead read, uint64_t material, CoTlEntry* e);
/* The cache's terrain-key slots into `out` (`max` at most): how many there are (more than `max`: the rest is left
 * out), -1 when the table does not read. `slots`: the table's slot count. */
int co_tl_cache(CoTlRead read, uint64_t holder, CoTlEntry* out, int max, uint32_t* slots);
/* The slot of `key` alone: 1 found, 0 not there, -1 unread. */
int co_tl_cache_key(CoTlRead read, uint64_t holder, uint64_t key, CoTlEntry* e);
/* A terrain's material list (`second`: the one at +0xA78) into `out`: its count, -1 unread or over CO_TL_LIST_MOST. */
int co_tl_materials(CoTlRead read, uint64_t terrain, int second, CoTlMaterial* out, int max);
/* The render config table's value slot of `key`, found as exe 0xA6E30 finds it and never inserted: its address, 0
 * when the key is not there; `state` -1 when the table does not read. */
uint64_t co_tl_slot(CoTlRead read, uint64_t table, uint32_t key, int* state);
/* Every terrain the world's map lists (+0x230550, exe 0x1BCF30's layout) with its entry's two maps: the count,
 * -1 unread. */
int co_tl_terrains(CoTlRead read, uint64_t world, CoTlTerrain* out, int max);
/* Lines of one kind a level may write: 1 while under `most`, 2 for the one that reaches it (the line says it is the
 * last), 0 after. A new `level` starts every kind again. Safe from several threads. */
#define CO_TL_KINDS 8
typedef struct CoTlPace { long level; uint32_t used[CO_TL_KINDS]; } CoTlPace;
int co_tl_pace(CoTlPace* pace, long level, int kind, uint32_t most);

#endif
