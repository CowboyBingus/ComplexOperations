#include "co_terrainlog.h"

int co_tl_terrain_key(uint64_t key) { return (key >> 32 & 0x00FFFFFFu) == 0x00FFBAABu; }

int co_tl_material(CoTlRead read, uint64_t material, CoTlEntry* e)
{
    e->handle = 0; e->block = 0; e->id = 0;
    return material != 0 && !read((uintptr_t)material + 4, &e->handle, 4) && !read((uintptr_t)material + 0x10, &e->block, 8) &&
        !read((uintptr_t)material + 0x38, &e->id, 8);
}

static int CacheTable(CoTlRead read, uint64_t holder, uint64_t* table, uint32_t* slots)
{
    return holder != 0 && !read((uintptr_t)holder + 0x98, table, 8) && !read((uintptr_t)holder + 0xA8, slots, 4) && *table != 0 &&
        *slots != 0 && *slots <= CO_TL_CACHE_SLOTS;
}

static int CacheSlot(CoTlRead read, uint64_t table, uint32_t i, CoTlEntry* e)
{
    uint64_t raw[3];
    if (read((uintptr_t)(table + (uint64_t)i * 24), raw, sizeof raw)) return 0;
    e->key = raw[0]; e->material = raw[1]; e->refs = (int32_t)(uint32_t)raw[2]; e->generation = (uint32_t)(raw[2] >> 32);
    return 1;
}

int co_tl_cache(CoTlRead read, uint64_t holder, CoTlEntry* out, int max, uint32_t* slots)
{
    uint64_t table = 0;
    uint32_t n = 0;
    int found = 0;
    if (slots != NULL) *slots = 0;
    if (!CacheTable(read, holder, &table, &n)) return -1;
    if (slots != NULL) *slots = n;
    for (uint32_t i = 0; i < n; i++)
    {
        CoTlEntry e;
        if (!CacheSlot(read, table, i, &e)) return -1;
        if (!co_tl_terrain_key(e.key)) continue;
        if (found < max) { co_tl_material(read, e.material, &e); out[found] = e; }
        found++;
    }
    return found;
}

/* The game probes from key % slots and stops at an empty slot (0x1DE1AB..0x1DE1F5); so does this. */
int co_tl_cache_key(CoTlRead read, uint64_t holder, uint64_t key, CoTlEntry* e)
{
    uint64_t table = 0;
    uint32_t n = 0;
    if (!CacheTable(read, holder, &table, &n)) return -1;
    uint32_t i = (uint32_t)(key % n);
    for (uint32_t tries = 0; tries < n; tries++, i = (i + 1) % n)
    {
        if (!CacheSlot(read, table, i, e)) return -1;
        if (e->key == key) { co_tl_material(read, e->material, e); return 1; }
        if (e->key == 0) return 0;
    }
    return 0;
}

int co_tl_materials(CoTlRead read, uint64_t terrain, int second, CoTlMaterial* out, int max)
{
    const uintptr_t list = (uintptr_t)terrain + (second ? 0xA78u : 0xA60u);
    uint32_t count = 0;
    uint64_t data = 0;
    if (terrain == 0 || read(list, &count, 4) || read(list + 8, &data, 8) || count > CO_TL_LIST_MOST || (count != 0 && data == 0)) return -1;
    for (uint32_t i = 0; i < count && (int)i < max; i++)
    {
        CoTlMaterial* m = &out[i];
        *m = (CoTlMaterial){0};
        if (read((uintptr_t)(data + (uint64_t)i * 16), &m->name, 4) || read((uintptr_t)(data + (uint64_t)i * 16 + 8), &m->material, 8)) return -1;
        if (m->material == 0 || read((uintptr_t)m->material + 4, &m->handle, 4) || read((uintptr_t)m->material + 0x10, &m->block, 8) || m->block == 0) continue;
        if (read((uintptr_t)m->block + 0x48, &m->entries, 8) || read((uintptr_t)m->block + 0x50, &m->count, 4) || read((uintptr_t)m->block + 0x58, &m->data, 8))
        { m->entries = m->data = ~0ull; m->count = ~0u; } /* a block that does not read is the finding itself */
    }
    return (int)count;
}

uint64_t co_tl_slot(CoTlRead read, uint64_t table, uint32_t key, int* state)
{
    uint64_t entries = 0;
    uint32_t count = 0, buckets = 0, next = 0, held = 0;
    *state = -1;
    if (table == 0 || read((uintptr_t)table + 8, &entries, 8) || read((uintptr_t)table + 0x18, &count, 4) || read((uintptr_t)table + 0x1C, &buckets, 4)) return 0;
    *state = 0;
    if (count == 0) return 0;
    if (entries == 0 || buckets == 0) { *state = -1; return 0; }
    uint32_t i = key % buckets;
    if (read((uintptr_t)(entries + (uint64_t)i * 0x20 + 0x18), &next, 4)) { *state = -1; return 0; }
    if (next == 0xFFFFFFFEu) return 0;
    for (uint32_t steps = 0; steps <= count; steps++)
    {
        const uint64_t at = entries + (uint64_t)i * 0x20;
        if (read((uintptr_t)at, &held, 4) || read((uintptr_t)at + 0x18, &next, 4)) { *state = -1; return 0; }
        if (held == key) { *state = 1; return at + 8; }
        if (next == 0x7FFFFFFFu) return 0;
        i = next;
    }
    *state = -1;
    return 0;
}

int co_tl_terrains(CoTlRead read, uint64_t world, CoTlTerrain* out, int max)
{
    const uintptr_t map = (uintptr_t)world + 0x230550u;
    uint64_t entries = 0;
    uint32_t count = 0, buckets = 0, next = 0;
    int found = 0;
    if (world == 0 || read(map + 8, &entries, 8) || read(map + 0x18, &count, 4) || read(map + 0x1C, &buckets, 4)) return -1;
    if (count == 0) return 0;
    if (entries == 0 || buckets == 0 || buckets > (1u << 20) || count > (1u << 20)) return -1;
    for (uint32_t head = 0; head < buckets && (uint32_t)found < count; head++)
    {
        uint32_t i = head;
        for (uint32_t steps = 0; steps <= count; steps++)
        {
            const uint64_t at = entries + (uint64_t)i * 0x170;
            if (read((uintptr_t)at + 0x168, &next, 4)) return -1;
            if (next == 0xFFFFFFFEu) break; /* an empty bucket */
            CoTlTerrain t = {0};
            if (read((uintptr_t)at, &t.handle, 8) || read((uintptr_t)at + 0x40, &t.heightmap, 8) || read((uintptr_t)at + 0x48, &t.material, 8)) return -1;
            if (found < max) out[found] = t;
            found++;
            if (next == 0x7FFFFFFFu) break;
            i = next;
        }
    }
    return found;
}

int co_tl_pace(CoTlPace* pace, long level, int kind, uint32_t most)
{
    if (kind < 0 || kind >= CO_TL_KINDS || most == 0) return 0;
    if (__atomic_exchange_n(&pace->level, level, __ATOMIC_SEQ_CST) != level)
        for (int k = 0; k < CO_TL_KINDS; k++) __atomic_store_n(&pace->used[k], 0u, __ATOMIC_SEQ_CST);
    const uint32_t n = __atomic_add_fetch(&pace->used[kind], 1u, __ATOMIC_SEQ_CST);
    if (n > most) { __atomic_store_n(&pace->used[kind], most + 1, __ATOMIC_SEQ_CST); return 0; } /* never wraps */
    return n == most ? 2 : 1;
}
