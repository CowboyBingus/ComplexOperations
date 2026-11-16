/* The render frame instrument's entries from hooks other parts own (xl_render_frame.c): the render message dispatch
 * (xl_span_rescue.c), the frame loop's fence wait (xl_free.c) and the device-chain rounds (living_world_renderhelp.c).
 * Research and test builds only; a package build has none of it. */
#if defined(CO_RESEARCH) || defined(CO_TEST)
typedef uint64_t (*XrfMessageFn)(uint64_t, uint64_t, uint64_t);
static volatile LONG g_xrfOn;    /* the instrument's hooks are in and counting */
static volatile LONG g_xrfViaSr; /* the stranded spans' dispatcher hook calls XrfMessage (else the instrument has its own) */
static uint64_t XrfMessage(uint64_t r, uint64_t message, uint64_t payload, XrfMessageFn orig);
static void XrfFence(int64_t in, int64_t out);
static void XrfRound(uintptr_t ret, uintptr_t dev);
/* Far-grass bake views' scatter work, from any thread inside the engine's update (difficulty_world_diag.c,
 * difficulty_world_terrain.c): an update of a bake view's layer, and its cells, instances and the mod's own ticks. */
static int64_t XrfClock(void);
static void XrfBakeUpdate(void);
static void XrfBakeAdd(uint64_t cells, uint64_t instances, int64_t ticks);
#endif
