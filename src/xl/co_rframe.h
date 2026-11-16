/* The render thread's frame, split into its parts (research and test builds only; a package has none of it). The
 * render thread fills one CoRfFrame a render frame (proto/xl_render_frame.c) and hands it over a lock-free ring; the
 * game thread adds it here, and every 10 s the window's lines give each part's mean, p99 and worst, the parts of the
 * slowest 1% of render frames, the counts, and the game thread's frame-fence waits. */
#ifndef CO_RFRAME_H
#define CO_RFRAME_H

#include <stdint.h>

/* The timed parts, ticks a render frame. Messages by type (word [msg+0xC] at the dispatcher 0x113010), then waits
 * and work inside them, each named by its call site's return address (exe RVAs). */
enum
{
    CO_RF_M07, CO_RF_M08, CO_RF_M09, CO_RF_M0A, CO_RF_M0B, CO_RF_M0E, CO_RF_M10, CO_RF_MOTHER,
    CO_RF_CULL,     /* 0x627160 from 0x1EC4AA (ret 0x1EC4AF): message 7's cull jobs */
    CO_RF_LODA,     /* 0x627160 from 0x1E2FA1 (ret 0x1E2FA6) */
    CO_RF_LODB,     /* 0x6097A0 from 0x1E3E64 (ret 0x1E3E69) */
    CO_RF_TREES,    /* 0x213540 from 0x1E3E2D (ret 0x1E3E32): the SpeedTree view task's spin 0x2135E0 */
    CO_RF_JOIN,     /* 0x252460 from 0x1EDA81 (ret 0x1EDA86): the generator join */
    CO_RF_SCATWAIT, /* 0x6097A0 from 0x21140C (ret 0x211411): the scatter graph wait of 0x2113C0 (message 7 calls it at
                       0x1ED7B2) for a view without flag bit 0, a far-grass bake view among them */
    CO_RF_DISPATCH, /* 0x6CC130 and 0x6CA5F0 (one caller each, 0x6CED47 and 0x6CED58) */
    CO_RF_APPLY,    /* 0x1E84A0 on the render thread: message 8's object messages */
    CO_RF_SPIN8,    /* 0x1E95A0 .. the last 0x528210 round at 0x1E95F8: message 8's inline device-chain spin (0x1E95B6) */
    CO_RF_CHAIN,    /* 0x10BB30: the device-chain spin */
    CO_RF_GPUWAIT,  /* 0x6C0590 from 0x6C5F3E (ret 0x6C5F43): present's frames-in-flight wait */
    CO_RF_DRAIN,    /* 0x6BEED0 from 0x6C6018 (ret 0x6C601D): the submit drain before Present1 */
    CO_RF_PRESENT1, /* ret 0x6C601D to the call at 0x6C6178: Present1 (vtable +0xB0, 0x6C60A5) and the calls after it */
    CO_RF_TIMED,
    /* the window's columns past the timed parts */
    CO_RF_PERIOD = CO_RF_TIMED, /* message 0x0B's entry to the next */
    CO_RF_OUTSIDE,              /* period less every message: the loop's idle poll (0x626900 at 0x113F1D) and the loop */
    CO_RF_GPU,                  /* [[r+0x1168]+0x1480] at the frame's end, ms */
    CO_RF_BAKE,                 /* 1: a far-grass bake view's scatter update ran in the frame */
    CO_RF_BAKECELLS,            /* its layers' cells (nodes the view test got, all layers) */
    CO_RF_BAKEINST,             /* its instances (the brushes' callback's, all layers) */
    CO_RF_BAKECO,               /* the mod's own wrappers' time in it, us summed over the threads */
    CO_RF_COLS
};

#define CO_RF_TYPES 0x16        /* message 8 item types 1..0x15 (0x1E84A0's table at 0x1E9484); 0 counts any other */
#define CO_RF_WALK 256          /* items walked a 0x1E84A0 call at most: the walk's cost stays bounded */
#define CO_RF_FRAMES 4096       /* render frames and fence waits kept a window: 400 a second for 10 s */
#define CO_RF_WINDOW_S 10.0
#define CO_RF_STALL_MS 25.0     /* a render frame this long is sampled (once a frame, CO_RF_STALL_SAMPLES a window) */
#define CO_RF_STALL_SAMPLES 3
#define CO_RF_BAKE_ADDED_MS 3.0 /* a bake view may add this much to its render frame's mean (the far-grass goal) */
#define CO_RF_ITEM_NS 100.0     /* a walked item's cost bound: two cache misses the game's own walk then does not take */

typedef struct
{
    int64_t t[CO_RF_TIMED];
    int64_t period;
    float gpuMs;                /* < 0: not read */
    uint32_t records, recordsMost;
    uint16_t types[CO_RF_TYPES];
    uint16_t clocks, walked, messages, dispatches, applies, applyCut, rounds8, foreign;
    uint32_t bakeUpdates, bakeCells, bakeInst; int64_t bakeTicks; /* far-grass bake views (CO_RF_BAKE..) */
    uint16_t spinEndsAtClose;   /* message 8 spins whose last round was not seen as the last: closed at the next event */
} CoRfFrame;

/* What the game thread reads at a window's close, beside the frames. */
typedef struct
{
    uint64_t views, cells; uint32_t cellsMost;  /* SpeedTree: 0x9548C0 from 0x212560, q[owner+0x90] after it */
    uint32_t lost;                               /* render frames the full ring dropped */
    int pools;                                   /* 1 read: the main world's culling sets 0..4 */
    uint32_t shown[5], shownMost[5], parked[5], parkedMost[5];
    int stamps;                                  /* 1 read, 0 not, -1 the streamer's lock was busy */
    uint32_t band[5], farCount, farLive;              /* far window stamps by the nearest player: <550, -800, -1100, -1500, more */
} CoRfCounts;

/* `freq` QPC ticks a second; `clockNs` one clock read's cost, for the instrument's own cost; `timed` the parts with a
 * hook in (bit per CO_RF_*; the rest print "-"). Starts a window at `now`. */
void co_rframe_reset(int64_t freq, double clockNs, uint32_t timed, int64_t now);
void co_rframe_timed(uint32_t timed);
void co_rframe_frame(const CoRfFrame* f);
/* A game-thread frame-fence wait (exe 0x8A7C4), ticks. */
void co_rframe_fence(int64_t ticks);
/* 1 when the window started at least CO_RF_WINDOW_S before `now`. */
int co_rframe_due(int64_t now);
/* Logs the window's lines (none for a window without render frames or fence waits) and starts the next; the lines
 * written. */
int co_rframe_close(int64_t now, const CoRfCounts* c);
/* A known wait's name for a return address or RIP (exe RVA), NULL none: the stall sample's reading. */
const char* co_rframe_wait_name(uint32_t rva);
/* The k-th smallest of n (0-based); reorders v. */
float co_rframe_select(float* v, uint32_t n, uint32_t k);

#ifdef CO_TEST
/* The last closed window: a column's mean, p99, worst over all its frames, and its mean over the slowest 1%. */
void co_rframe_test_col(int col, double* mean, double* p99, double* worst, double* slowMean);
void co_rframe_test_fence(uint32_t* waits, double* mean, double* p99, double* worst);
uint32_t co_rframe_test_slow(void);
/* The last window's frames with a bake view, those of them over CO_RF_STALL_MS, and the ms a bake view added. */
void co_rframe_test_bake(uint32_t* frames, uint32_t* over, double* added);
#endif

#endif
