/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The D-50 on fm1-base: the panel, MIDI in, and the engine's 32 kHz stereo resampled to the platform's 44.1 kHz. The engine itself is
 * fm1_open/app/d50 (d50.h, d50.cpp's FM1_BASE part), compiled in d50_unit.cpp. Part of the platform's unity build (device/unity.c), after
 * the platform: gfx.c and announce.c are in scope. FM1-BASE.md. */
#include "../fm1_open/app/d50/d50.h"

#define SET_MAGIC 0x44353031u            /* "D501": the settings record */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t program;                     /* 0 .. 63 */
    uint8_t rsv[2];
} settings_t;
static settings_t set;
static uint8_t set_dirty;

/* ------------------------------------------------------------------ the sound --- */
/* The engine renders at 32 kHz; a block of APP_BLOCK output frames takes 92 or 93 of its frames, resampled here as fm1_open's sf2out.c does
 * (the same filter, resample_table.inc from tools/make_resampler.js): 16-bit samples, one array a channel, after the last RESAMPLE_TAPS
 * frames before them; position is where output frame 0 of the next block sits, in frames, Q24. Silence skips the filter. */
#include "../fm1_open/app/resample_table.inc"
enum { RS_HISTORY = RESAMPLE_TAPS, PLAYER_FRAMES = APP_BLOCK * D50_RATE / 44100u + 3u };
#define RS_STEP ((uint32_t)((((uint64_t)D50_RATE << 24) + 22050u) / 44100u))
static int32_t stereo[PLAYER_FRAMES * 2];
static int16_t left_in[RS_HISTORY + PLAYER_FRAMES], right_in[RS_HISTORY + PLAYER_FRAMES];
static uint32_t position = (uint32_t)(RESAMPLE_TAPS / 2 - 1) << 24;
static unsigned silent = RS_HISTORY;
static volatile int32_t program_asked = -1;  /* a program change from MIDI, for the main loop (building a program takes a while) */
static volatile uint32_t midi_count;

static inline int32_t sat24(int32_t v) { return v > 8388607 ? 8388607 : v < -8388608 ? -8388608 : v; }

static void midi_in(void)                    /* audio interrupt: the packets that came in */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (st == 0xF8u || st == 0xFEu)
            continue;
        midi_count++;
        switch (st & 0xF0u) {
        case 0x90u: d50_note(d1, d2); break;
        case 0x80u: d50_note(d1, 0); break;
        case 0xE0u: d50_bend(d1 | d2 << 7); break;
        case 0xC0u: program_asked = (int32_t)d1; break;
        case 0xB0u: if (d1 == 120u || d1 == 123u) d50_panic(); break;
        }
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, j, at;
    unsigned m;
    int32_t gain = (int32_t)master_q12;          /* 0..4096 */
    midi_in();
    if (n > APP_BLOCK)
        n = APP_BLOCK;
    /* the last output frame reads up to sample floor(last) + TAPS / 2, which must be the newest one: HISTORY + m - 1 */
    m = ((position + (n - 1u) * RS_STEP) >> 24) + RESAMPLE_TAPS / 2 + 1u - RS_HISTORY;
    for (i = 0; i < 2u * m; i++)
        stereo[i] = 0;
    d50_render(stereo, m);
    {
        int16_t *l = left_in + RS_HISTORY, *r = right_in + RS_HISTORY;
        for (i = 0; i < m; i++) {                /* Q23 (saturated) to Q15 */
            l[i] = (int16_t)(stereo[2 * i] >> 8);
            r[i] = (int16_t)(stereo[2 * i + 1] >> 8);
            silent = (l[i] | r[i]) ? 0u : silent < RS_HISTORY + PLAYER_FRAMES ? silent + 1u : silent;
        }
    }
    at = position;
    if (silent < RS_HISTORY + m) {
        for (j = 0; j < n; j++, at += RS_STEP) {
            const int16_t *c = resample_table[(at >> 16) & (RESAMPLE_PHASES - 1u)];
            const unsigned from = (at >> 24) - (RESAMPLE_TAPS / 2 - 1);
            const int16_t *xl = left_in + from, *xr = right_in + from;
            int32_t sl = 0, sr = 0;
            unsigned k;
            for (k = 0; k < RESAMPLE_TAPS; k++) {   /* Q15 samples times Q14 taps: within 31 bits */
                sl += xl[k] * c[k];
                sr += xr[k] * c[k];
            }
            /* Q23, then MASTER (Q12) without leaving 32 bits: Q18 times Q12 */
            lr[2 * j] = ((sat24(sl >> 6) >> 5) * gain) >> 7;
            lr[2 * j + 1] = ((sat24(sr >> 6) >> 5) * gain) >> 7;
        }
    } else {
        for (j = 0; j < 2u * n; j++)
            lr[j] = 0;
        at += n * RS_STEP;
    }
    position = at - (m << 24);
    for (i = 0; i < RS_HISTORY; i++) {           /* the newest frames are the next block's history */
        left_in[i] = left_in[m + i];
        right_in[i] = right_in[m + i];
    }
    d50_guard(plat_cpu_pct());                   /* the CPU guard: sheds released voices, then caps them, over 85 / 92 % */
}

void app_panic(void) { d50_panic(); }

/* ------------------------------------------------------------------ settings --- */
static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC || set.octave < -2 ||
        set.octave > 2 || set.program > 63u) {
        set.magic = SET_MAGIC;
        set.octave = 0;
        set.program = 0;
    }
}

static int bound;                            /* the D-50's data found and the engine started */
void app_init(void)
{
    settings_load();
#if FM1_CPU1
    plat_cpu2_set(1);                        /* the second core renders every second sounding voice (d50.cpp split_begin); else one core */
#endif
    d50_program(set.program);                /* (before the data: kept for the bind) */
    bound = d50_bind_flash();
}

/* ------------------------------------------------------------------------ UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint8_t redraw;
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while ((*d++ = *s++) != 0) {
    }
}
static void uint_text(uint32_t v, char *b)
{
    char t[12];
    int i = 0, k = 0;
    do {
        t[i++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (i)
        b[k++] = t[--i];
    b[k] = 0;
}

void app_say(const char *a, const char *b)
{
    unsigned i;
    for (i = 0; i < 23u && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1200u;
    ui.redraw = 1;
    if (ann_on()) {                          /* and to a screen reader (announce.c, fm1-reader) */
        char t[48];
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void note(uint32_t note, uint32_t vel)    /* main loop: the queue is the interrupt's too */
{
    uint32_t f = plat_lock();
    d50_note(note, vel);
    plat_unlock(f);
}

static void program_step(int32_t to)
{
    uint32_t n = d50_programs();
    set.program = (uint8_t)(((to % (int32_t)n) + (int32_t)n) % (int32_t)n);
    set_dirty = 1;
    d50_program(set.program);
    ui.redraw = 1;
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), k;
    int32_t e;
    for (k = 0; k < NKEYS; k++) {                /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k, n = (uint32_t)(53 + (int32_t)k + 12 * set.octave);
        if ((keys & bit) && !(ui.keys & bit))
            note(n, 100u);
        else if (!(keys & bit) && (ui.keys & bit))
            note(n, 0u);
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (k == B_OCTDN && set.octave > -2) {
            set.octave--;
            set_dirty = 1;
        } else if (k == B_OCTUP && set.octave < 2) {
            set.octave++;
            set_dirty = 1;
        } else if (k == B_SAVE) {
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SAVED", "");
            } else {
                app_say("SAVE", "FAILED");
            }
        }
        ui.redraw = 1;
    }
    if ((e = plat_enc(EN_SELECT)) != 0 || (e = plat_enc(EN_PRESET)) != 0)
        program_step((int32_t)set.program + e);
    if (program_asked >= 0) {
        int32_t p = program_asked;
        program_asked = -1;
        program_step(p);
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);
    plat_leds(btn | (set_dirty ? 1u << B_SAVE : 0u), keys);
}

void app_input_only(void) { input(); }

static void draw(void)
{
    char t[32];
    uint32_t now = plat_ms(), p = set.program;
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        cv_text(10, 6, &FONT_B, "D-50", C_WHITE);
        cv_text(70, 10, &FONT_XS, "fm1-base", C_DIM);
    }
    cv_blit(0, 0);
    cv_begin(240, 120, C_BLACK);
    if (!bound) {
        cv_text(10, 20, &FONT_M, "NO D-50 DATA", C_HI);
    } else {
        t[0] = (char)('1' + p / 8u);
        t[1] = (char)('1' + p % 8u);
        t[2] = 0;
        cv_text(10, 4, &FONT_XS, "PATCH", C_GRAY);
        cv_text(10, 18, &FONT_L, t, C_HI);
        cv_text(10, 60, &FONT_B, d50_program_name(p), C_WHITE);
    }
    cv_blit(0, 40);
    cv_begin(240, 40, C_BLACK);
    t[0] = 0;
    text_cat(t, "VOICES ");
    uint_text(d50_voices_sounding(), t + 7);
    text_cat(t, "  CPU ");
    uint_text(plat_cpu_pct(), t + str_len(t));
    text_cat(t, "%  ");
    uint_text(d50_cores(), t + str_len(t));
    text_cat(t, d50_cores() > 1u ? " CORES" : " CORE");
    cv_text(10, 4, &FONT_XS, t, C_GRAY);
    t[0] = 0;
    text_cat(t, "OCT ");
    if (set.octave < 0)
        text_cat(t, "-");
    uint_text((uint32_t)(set.octave < 0 ? -set.octave : set.octave), t + str_len(t));
    text_cat(t, "  LIMIT ");
    uint_text(d50_voice_limit(), t + str_len(t));
    text_cat(t, d50_reverb() ? "  REVERB" : "");
    cv_text(10, 20, &FONT_XS, t, C_GRAY);
    cv_blit(0, 180);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_ms, last_midi;
    uint32_t now = plat_ms();
    input();
    if (now - last_ms >= 250u) {                 /* the voices and the load, four times a second */
        last_ms = now;
        ui.redraw = 1;
    }
    if (midi_count != last_midi) {
        last_midi = midi_count;
        ui.redraw = 1;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}
