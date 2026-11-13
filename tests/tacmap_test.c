/* The tactical map on M (src/xl/proto/difficulty_tacmap.c): the screen goes to HellForge through add_screen, a refusal
 * is logged with its reason and leaves the session without a map, and the M key's path runs as the overlay host runs
 * it: can_open, opened, draw frames with the mouse, the closing, opened(0). The waypoint is read where MapPing writes
 * it: the avatar manager's records, moved in a build for eight. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestTacGame(uintptr_t game);
void CoXlTestTacTick(void);
int CoXlTestTacScreen(void);
int CoXlTestTacPing(float* x, float* y, int* remove);
void CoXlTestTacRestart(void);
void CoXlTestTacPingAs(int slot);
int CoXlTestTacWaypoint(uint32_t i, float* x, float* y);
void co_managers_test_force(int state);
void co_managers_test_reset(void);
int co_counts_test_players(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* game.dll's places the map reads (difficulty_tacmap.c) */
#define TAC_LEVELS 0x3326340u
#define TAC_WIDGETS 0x3326E68u
#define TAC_AVATARS 0x3326D20u
#define TAC_MAP 0x346D538u
#define TAC_PING 0x18B97A0u
/* The avatars' records in the manager: the game's offset, and the one a build for eight gives them (co_manager_sites.h:
 * every record field's site adds 11079008, MapPing's own among them). Flags +0 (4: a waypoint), the waypoint +0x50. */
#define RECORDS 0x547830u
#define RECORDS_MOVED 0xFD8590u
#define AVATARS_SIZE 0x1000000u

/* A mission's world and level, outside the image as the game's heap is. */
static uint8_t g_world[0xAE300], g_lg[0x3000], g_chat[0x14000], g_widgets[0x3000];

static void PutU32(uint8_t* at, uint32_t v) { memcpy(at, &v, 4); }
static void PutF(uint8_t* at, float v) { memcpy(at, &v, 4); }
static void PutPtr(uint8_t* at, const void* p) { const uint64_t v = (uint64_t)(uintptr_t)p; memcpy(at, &v, 8); }

/* A waypoint at x, y in avatar slot's record, the records at `records` of the manager. */
static void Waypoint(uint8_t* avatars, uint32_t records, uint32_t slot, float x, float y)
{
    uint8_t* r = avatars + records + 0x78u * slot;
    PutU32(r, 4u);
    PutF(r + 0x50, x);
    PutF(r + 0x54, y);
}

/* A mission of a `size` m world, its level ready, the whole of it playable; size 0: no mission. */
static void Mission(uint8_t* game, float size)
{
    memset(g_world, 0, sizeof g_world);
    memset(g_lg, 0, sizeof g_lg);
    memset(game + TAC_LEVELS, 0, 8);
    if (size <= 0.0f) return;
    PutPtr(game + TAC_LEVELS, g_world);
    PutU32(g_world + 0xAC21C, 4);
    PutPtr(g_world + 0xAE288, g_lg);
    PutU32(g_lg + 0x1E4, 3);
    PutF(g_lg + 0x1E0, size);
    PutF(g_lg + 0x2D70, 1.0f);
}

/* The chat widget among the game's widgets, its line open for typing or not. */
static void Chat(uint8_t* game, int typing)
{
    memset(g_widgets, 0, sizeof g_widgets);
    memset(g_chat, 0, sizeof g_chat);
    PutPtr(game + TAC_WIDGETS, g_widgets);
    PutU32(g_widgets + 0x2BE8, 1);
    PutPtr(g_widgets + 0x2BF0, g_chat);
    PutU32(g_widgets + 0x2BF8, 0xC3);
    g_chat[0x139B8] = (uint8_t)typing;
}

/* A recording HfDraw: how often each kind was drawn and whether the title was. */
static struct { int shapes, texts, backdrops, title, clips; } g_drawn;
static void DrawRect(float x0, float y0, float x1, float y1, HfColour fill, HfColour outline, float thickness) { g_drawn.shapes++; }
static void DrawLines(const HfPoint* p, uint32_t n, HfColour c, float thickness, int closed) { g_drawn.shapes++; }
static void DrawPolygon(const HfPoint* p, uint32_t n, HfColour fill) { g_drawn.shapes++; }
static void DrawCircle(float x, float y, float r, HfColour fill, HfColour outline, float thickness) { g_drawn.shapes++; }
static float TextWidth(const char* s, float size, uint32_t face) { return (float)strlen(s) * size * 0.5f; }
static float DrawLabel(float x, float y, const char* s, float size, uint32_t face, uint32_t align, HfColour c)
{
    g_drawn.texts++;
    if (strcmp(s, "TACTICAL MAP") == 0 && face == HF_FACE_TITLE) g_drawn.title++;
    return TextWidth(s, size, face);
}
static int g_imageToken;
static HfImage* ImageCreate(uint32_t w, uint32_t h, const void* rgba) { return (HfImage*)&g_imageToken; }
static void ImageUpdate(HfImage* image, const void* rgba) {}
static void ImageFree(HfImage* image) {}
static void Image(HfImage* image, const HfRect* box, const HfRect* part, HfColour tint) { g_drawn.shapes++; }
static void ClipPush(float x0, float y0, float x1, float y1) { g_drawn.clips++; }
static void ClipPop(void) { g_drawn.clips--; }
static void Backdrop(float dim, float desaturate, float blur) { g_drawn.backdrops++; }
static const HfDraw kDraw = {sizeof(HfDraw), 1920.0f, 1080.0f, 1.0f, DrawRect, DrawLines, DrawPolygon, DrawCircle, DrawLabel, TextWidth,
    ImageCreate, ImageUpdate, ImageFree, Image, ClipPush, ClipPop, Backdrop};

/* One frame of the open screen, as the host draws it: the game thread's tick first, then draw. */
static int Frame(const HfInput* in, float dt)
{
    CoXlTestTacTick();
    return g_fake.screen.draw(g_fake.screen.user, &kDraw, in, dt);
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* game = g_fake.image;
    HfApi* api = (HfApi*)g_coApi;

    g_case = "no install yet: the tick asks for no screen";
    {
        CoXlTestTacTick();
        CHECK(g_fake.screenAdds == 0);
        CHECK(CoXlTestTacScreen() == 0);
    }

    g_case = "a HellForge whose table ends before add_screen: logged, never called, no screen";
    {
        CoXlTestTacGame((uintptr_t)game);
        api->size = (uint32_t)offsetof(HfApi, add_screen);
        CoXlTestTacTick();
        CoXlTestTacTick();
        CHECK(g_fake.screenAdds == 0);
        CHECK(CoXlTestTacScreen() == 0);
        CHECK(fake_host_logged("difficulty: tactical map: this HellForge has no add_screen (mod SDK 17); no map screen"));
        api->size = sizeof(HfApi);
    }

    g_case = "HellForge refuses the screen: the reason's place logged, asked once, no screen";
    {
        CoXlTestTacRestart();
        g_fake.log[0] = 0;
        g_fake.screenAnswer = 0;
        CoXlTestTacTick();
        CoXlTestTacTick();
        CHECK(g_fake.screenAdds == 1);
        CHECK(CoXlTestTacScreen() == 0);
        CHECK(fake_host_logged("difficulty: tactical map: HellForge refused the screen (no overlay host, or M is another screen's key: modloader.log says which); no map screen"));
        CHECK(!fake_host_logged("on M"));
        CHECK(!fake_host_logged("hellforge_overlay.dll is not beside the loader"));
    }

    g_case = "the screen goes in through add_screen: M, modal, the mod's own, every callback";
    {
        CoXlTestTacRestart();
        g_fake.log[0] = 0;
        g_fake.screenAdds = 0;
        g_fake.screenAnswer = 3;
        CoXlTestTacTick();
        CoXlTestTacTick();
        CHECK(g_fake.screenAdds == 1);
        CHECK(CoXlTestTacScreen() == 3);
        CHECK(g_fake.screenMod == g_coMod);
        CHECK(g_fake.screen.size == sizeof(HfScreen));
        CHECK(g_fake.screen.key == 'M');
        CHECK(g_fake.screen.flags == HF_SCREEN_MODAL);
        CHECK(g_fake.screen.name != NULL && strcmp(g_fake.screen.name, "Tactical map") == 0);
        CHECK(g_fake.screen.draw != NULL && g_fake.screen.opened != NULL && g_fake.screen.can_open != NULL);
        CHECK(fake_host_logged("difficulty: tactical map: on M (screen 3 of HellForge's overlay host)"));
    }

    const HfScreen* s = &g_fake.screen;
    g_case = "M outside a mission, on a 2 km world and while typing in chat: the key stays the game's";
    {
        Mission(game, 0.0f);
        CHECK(s->can_open(s->user) == 0);
        Mission(game, 2048.0f);
        CHECK(s->can_open(s->user) == 0);
        Mission(game, 16384.0f);
        Chat(game, 1);
        CHECK(s->can_open(s->user) == 0);
    }

    g_case = "M on a 16 km world: the map opens, draws its title over the backdrop and logs the opening";
    {
        Chat(game, 0);
        CHECK(s->can_open(s->user) == 1);
        g_fake.log[0] = 0;
        s->opened(s->user, 1);
        HfInput in = {sizeof in, 1134.0f, 549.0f};
        int drawn = 1;
        for (int i = 0; i < 30 && drawn; i++) drawn = Frame(&in, 1.0f / 60.0f);
        CHECK(drawn == 1);
        CHECK(g_drawn.backdrops == 30 && g_drawn.title > 0 && g_drawn.texts > 30 && g_drawn.shapes > 30);
        CHECK(g_drawn.clips == 0);
        CHECK(fake_host_logged("difficulty: tactical map: open: world 16384 m, playable 8192 m"));
    }

    g_case = "a right click on the map queues a waypoint at the world point under the cursor";
    {
        float x = 1e9f, y = 1e9f;
        int remove = -1;
        HfInput in = {sizeof in, 1134.0f, 549.0f};
        in.pressed = in.down = HF_MOUSE_RIGHT;
        g_fake.screen.draw(g_fake.screen.user, &kDraw, &in, 1.0f / 60.0f);
        CHECK(CoXlTestTacPing(&x, &y, &remove) == 1);
        CHECK(x > -20.0f && x < 20.0f && y > -20.0f && y < 20.0f && remove == 0);
        CoXlTestTacTick(); /* no own avatar on this machine: the tick drops it */
        CHECK(CoXlTestTacPing(&x, &y, &remove) == 0);
    }

    g_case = "a right click off the map's box queues none";
    {
        float x, y;
        int remove;
        HfInput in = {sizeof in, 100.0f, 549.0f};
        in.pressed = in.down = HF_MOUSE_RIGHT;
        g_fake.screen.draw(g_fake.screen.user, &kDraw, &in, 1.0f / 60.0f);
        CHECK(CoXlTestTacPing(&x, &y, &remove) == 0);
    }

    /* MapPing is the game's: here a ret, the waypoint already in a record. Not moved, then moved: a build for four never
     * moves the avatar manager, so both read the game's place there. */
    const int eight = co_counts_test_players() == 8;
    uint8_t* avatars = VirtualAlloc(NULL, AVATARS_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD was;
    CHECK(avatars != NULL && VirtualProtect(game + TAC_PING, 1, PAGE_EXECUTE_READWRITE, &was));
    game[TAC_PING] = 0xC3;
    PutPtr(game + TAC_AVATARS, avatars);
    PutPtr(game + TAC_MAP, g_lg);
    for (int moved = 0; moved < 2 && avatars != NULL; moved++)
    {
        co_managers_test_force(moved);
        const uint32_t here = moved && eight ? RECORDS_MOVED : RECORDS, there = here == RECORDS ? RECORDS_MOVED : RECORDS;
        float x = 0, y = 0;
        int remove;
        HfInput in = {sizeof in, 1134.0f, 549.0f};
        in.pressed = in.down = HF_MOUSE_RIGHT;

        g_case = !moved ? "the game's records: the waypoint MapPing set is the player's and the ping's log says so"
            : eight ? "a build for eight, moved: the waypoint is read at the moved records" : "a build for four, \"moved\": the game's records";
        CHECK(co_managers_offset(TAC_AVATARS, RECORDS) == here);
        memset(avatars, 0, AVATARS_SIZE);
        Waypoint(avatars, here, 4, 100.0f, 200.0f);
        Waypoint(avatars, here, 5, 1234.0f, -5678.0f);
        CHECK(CoXlTestTacWaypoint(5, &x, &y) == 1 && x == 1234.0f && y == -5678.0f);
        CHECK(CoXlTestTacWaypoint(4, &x, &y) == 1 && x == 100.0f && y == 200.0f);
        CHECK(CoXlTestTacWaypoint(3, &x, &y) == 0);
        g_fake.screen.draw(g_fake.screen.user, &kDraw, &in, 1.0f / 60.0f);
        CHECK(CoXlTestTacPing(&x, &y, &remove) == 1);
        g_fake.log[0] = 0;
        CoXlTestTacPingAs(5);
        CHECK(CoXlTestTacPing(&x, &y, &remove) == 0);
        CHECK(fake_host_logged("(slot 5): now set"));

        g_case = !moved ? "the game's records: a waypoint only at the moved place is none"
            : eight ? "a build for eight, moved: a waypoint only at the game's place is none" : "a build for four, \"moved\": none at the moved place";
        memset(avatars, 0, AVATARS_SIZE);
        Waypoint(avatars, there, 5, 1234.0f, -5678.0f);
        CHECK(CoXlTestTacWaypoint(5, &x, &y) == 0);
        g_fake.screen.draw(g_fake.screen.user, &kDraw, &in, 1.0f / 60.0f);
        g_fake.log[0] = 0;
        CoXlTestTacPingAs(5);
        CHECK(fake_host_logged("(slot 5): now none"));
    }
    co_managers_test_force(0);
    co_managers_test_reset();

    g_case = "closing (M or Esc): the map plays its close, then draw returns 0 and opened(0) follows";
    {
        HfInput in = {sizeof in, 1134.0f, 549.0f};
        in.closing = 1;
        int frames = 0, drawn = 1;
        while (drawn && frames < 120) { drawn = Frame(&in, 1.0f / 60.0f); frames++; }
        CHECK(drawn == 0);
        CHECK(frames > 1 && frames < 30); /* CLOSE_S of 0.25 s at 60 fps */
        s->opened(s->user, 0);
        CHECK(s->can_open(s->user) == 1); /* and M opens it again */
    }

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    printf("tacmap: all checks passed\n");
    return 0;
}
