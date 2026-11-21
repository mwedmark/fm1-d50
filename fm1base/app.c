/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The D-50 on fm1-base: the panel, MIDI in, and the engine's 32 kHz stereo resampled to the platform's 44.1 kHz. The engine itself is
 * fm1_open/app/d50 (d50.h, d50.cpp's FM1_BASE part), compiled in d50_unit.cpp. Part of the platform's unity build (device/unity.c), after
 * the platform: gfx.c and announce.c are in scope. FM1-BASE.md. */
#include "../fm1_open/app/d50/d50.h"

#define SET_MAGIC 0x44353031u            /* "D501": the settings record */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t program;                     /* 0 .. 63, in the bank */
    uint8_t bank;                        /* 0 .. d50_banks() - 1 (issue #7; was a reserved byte, 0) */
    uint8_t lcd;                         /* the D-50's display (issues #23, #26; was a reserved byte, 0): the view, the low bits its dot pitch */
} settings_t;
#define LCD_TRUE 0x80u                   /* the LCD at true size, 2 x 40 (#23) */
#define LCD_FOLDED 0x40u                 /* the LCD folded, 4 x 20 (#26) */
#define LCD_SHOWN (LCD_TRUE | LCD_FOLDED)
#define LCD_SCHEME_SHIFT 3u              /* the LCD's colours (#26): d50_lcd.h lcd_colours */
#define LCD_SCHEME_MASK (3u << LCD_SCHEME_SHIFT)
#define LCD_PITCH_MIN 1u                 /* (a dot's height in pixels; it is one pixel wide) */
#define LCD_PITCH_MAX 6u
#define LCD_PITCH_DEFAULT 1u             /* square dots, as the D-50's */
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
static volatile int32_t bank_asked = -1;     /* CC0 (bank select): the bank the next program change picks (issue #7) */
static volatile uint32_t midi_count;

static inline __attribute__((always_inline)) int32_t sat24(int32_t v) { return v > 8388607 ? 8388607 : v < -8388608 ? -8388608 : v; }

/* the profiling (d50.cpp FM1_D50_PROF): [8] the conversion and resampling here; plat_ticks24 (TIMER4, 24 MHz, d50.cpp's clock) */
extern volatile uint32_t d50_prof_sum[9];
extern volatile unsigned d50_prof_skip;

/* the 32 kHz frames to n output frames at 44.1 kHz from at, MASTER (Q12) applied: from RAM (PLAT_RAM_CODE), out of the way of the XIP
 * cache that the waves keep busy */
static PLAT_RAM_CODE uint32_t resample(int32_t *lr, uint32_t n, uint32_t at, int32_t gain)
{
    uint32_t j;
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
    return at;
}

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
        case 0xB0u:
            if (d1 == 120u || d1 == 123u)
                d50_panic();
            else if (d1 == 0u)
                bank_asked = (int32_t)d2;        /* (not the D-50's: its banks were cards; upstream PicoFaceD5's) */
            else
                d50_cc(d1, d2);                  /* the D-50's controls: d50.h d50_cc (issue #4) */
            break;
        case 0xD0u: d50_pressure(d1); break;     /* channel pressure: the D-50's aftertouch */
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
    const uint32_t resample_t0 = plat_ticks24();
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
        at = PLAT_FAR(resample)(lr, n, at, gain);
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
    d50_prof_sum[8] += plat_ticks24() - resample_t0;
    {                                            /* the CPU guard (d50.cpp d50_guard): a block missed since the last counts as 100 % */
        static uint32_t xruns_seen;
        uint32_t x = plat_xruns(), pct = plat_cpu_pct();
        if (x != xruns_seen) {
            xruns_seen = x;
            pct = 100u;
        }
        d50_guard(pct);
    }
}

void app_panic(void) { d50_panic(); }

/* ------------------------------------------------------- the time per block --- */
/* fm1_open's sf2_upload.c cmd 11 and 12, the same frames (so its pages and scripts read both):
 *   F0 7D S F 02 0B 00 F7  ->  F0 7D S F 02 4B v0..v10 F7, each 5 x 7 bits (least significant first): v0..v8 d50_prof_sum (then from zero),
 *                              v9 CPU average | peak << 12 | CPU1's share << 24, v10 voices | limit << 8 | cores << 16,
 *                              v11 the audio blocks missed since the start (plat_xruns: a render past its half)
 *   F0 7D S F 02 0C bits F7 ->  F0 7D S F 02 4C 00 F7: leave out 1 the reverb, 2 the chorus, 4 the EQ */
static uint32_t cpu_peak;                        /* the highest plat_cpu_pct since the last cmd 11 (app_frame) */
int app_sysex(const uint8_t *p, uint32_t n)
{
    if (d50_sysex(p, n))                         /* the D-50's own: DT1 and RQ1 (d50.h, issue #6); its answers go out in app_frame */
        return 1;
    if (n != 6u || p[0] != 0x7Du || p[1] != 'S' || p[2] != 'F' || p[3] != 2u)
        return 0;
    if (p[4] == 11u) {
        uint8_t r[6 + 12 * 5 + 1] = {0xF0, 0x7D, 'S', 'F', 2, 0x4B}, *d = r + 6;
        uint32_t v[12], i, k, f = plat_lock();
        for (i = 0; i < 9u; i++) {
            v[i] = d50_prof_sum[i];
            d50_prof_sum[i] = 0;
        }
        plat_unlock(f);
        v[9] = plat_cpu_pct() | cpu_peak << 12 | d50_split_share() << 24;
        v[10] = d50_voices_sounding() | d50_voice_limit() << 8 | d50_cores() << 16;
        v[11] = plat_xruns();
        cpu_peak = 0;
        for (i = 0; i < 12u; i++)
            for (k = 0; k < 5u; k++)
                *d++ = (uint8_t)((v[i] >> (7u * k)) & 0x7Fu);
        *d = 0xF7;
        (void)plat_sysex_send(r, sizeof r);
        return 1;
    }
    if (p[4] == 12u) {
        static const uint8_t ok[] = {0xF0, 0x7D, 'S', 'F', 2, 0x4C, 0, 0xF7};
        d50_prof_skip = p[5] & 7u;
        (void)plat_sysex_send(ok, sizeof ok);
        return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------- backup --- */
/* (the resource region's transfer is the restore's, so the backup is built: the settings are all it holds) */
static void settings_load(void);
int app_backup_playing(void) { return d50_voices_sounding() != 0u; }
void app_backup_flush(void)
{
    if (set_dirty && plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0)
        set_dirty = 0;
}
void app_backup_hold(int on) { (void)on; }       /* (only SAVE saves) */
void app_backup_restored(void)
{
    settings_load();
    d50_bank(set.bank);
    d50_program(set.program);
}

/* ------------------------------------------------------------------ settings --- */
static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC || set.octave < -2 ||
        set.octave > 2 || set.program > 63u) {
        set.magic = SET_MAGIC;
        set.octave = 0;
        set.program = 0;
        set.bank = 0;
        set.lcd = 0;
    }
}

static uint32_t lcd_pitch(void)
{
    uint32_t p = set.lcd & 7u;
    return p < LCD_PITCH_MIN || p > LCD_PITCH_MAX ? LCD_PITCH_DEFAULT : p;
}

/* The D-50's data (d50.cpp fm1_flash_map): the resource region's payload. A build with fm1.toml resources has plat_res, which takes only
 * the payload that build was made from (its length and CRC are compiled in). A build without them (GitHub's, which never holds Roland's
 * data: issue #36) reads the region as it lies: the commit record a local build's package or fm1_resources.py wrote (device/res.c:
 * "RES1", the payload's length and CRC-32, the record's own CRC), and the payload checked against it once. d50.cpp then checks its own
 * header ("FMD5"). So an update from GitHub keeps the data the FM-1 has and plays it. */
#if !FM1B_RES
static uint32_t res_crc32(const void *p, uint32_t n)   /* zlib's CRC-32, a bit at a time (once, at the start) */
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *b++;
        for (k = 0; k < 8u; k++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
#endif
const uint8_t *app_d50_data(uint32_t *len)
{
#if FM1B_RES
    return plat_res(len);
#else
    static uint8_t checked;
    static uint32_t found;
    if (!checked) {
        const uint8_t *r = plat_data(APP_RES_BASE);   /* (read in place: plat_data on the device, the emulator's data area on the web) */
        uint32_t h[8];
        checked = 1;
        if (r) {
            memcpy(h, r, sizeof h);
            if (h[0] == 0x31534552u && h[7] == res_crc32(h, 28u) && h[1] && h[1] <= APP_RES_SECTORS * 4096u - 32u &&
                res_crc32(r + 32, h[1]) == h[2])
                found = h[1];
        }
    }
    if (len)
        *len = found;
    return found ? plat_data(APP_RES_BASE + 32u) : 0;
#endif
}

static int bound;                            /* the D-50's data found and the engine started */
void app_init(void)
{
    settings_load();
#if FM1_CPU1
    plat_cpu2_set(1);                        /* the second core renders every second sounding voice (d50.cpp split_begin); else one core */
#endif
    d50_bank(set.bank);                      /* (before the data: kept for the bind, which checks it) */
    d50_program(set.program);
    bound = d50_bind_flash();
    set.bank = (uint8_t)d50_current_bank();
}

/* ------------------------------------------------------------------------ UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint8_t pressure;                            /* the aftertouch last sent from the keys (issue #5) */
    uint8_t redraw;
    uint8_t lcd_msg;                             /* the message in the LCD (#25): 0 the panel's, LCD_MSG_COMPLETE, LCD_MSG_ERROR | its number */
    uint8_t home_long;                         /* HOME held long enough to have changed the LCD's colours (this press) */
    uint32_t home_ms;                            /* when HOME was pressed */
    uint8_t page;                                /* the edit page (#31): d50_lcd.h LCD_PAGE_*, LCD_PAGE_PLAY none */
} ui;
#include "d50_lcd.h"
/* A parameter of the patch block changed on an edit page (#31): a DT1 into the temporary area (d50_sysex, #6), so the sound follows */
static void page_edit(uint32_t field, int32_t step)
{
    const lcd_page_t *g = &lcd_pages[ui.page];
    const uint8_t *p = d50_patch_bytes();
    uint8_t f[9] = {0x41, 0x10, 0x14, 0x12, 0, 3, 0, 0, 0};    /* 41 dev 14 12, address 00 03 nn (the patch block), the value, the sum */
    int32_t v;
    if (!p || field >= g->fields)
        return;
    v = clamp((int32_t)p[6u * 64u + g->f[field].byte] + step, g->f[field].lo, g->f[field].hi);
    f[6] = g->f[field].byte;
    f[7] = (uint8_t)v;
    f[8] = (uint8_t)((128u - ((f[4] + f[5] + f[6] + f[7]) & 127u)) & 127u);
    (void)d50_sysex(f, sizeof f);
    ui.redraw = 1;
}
#define HOME_LONG_MS 3000u
#define LCD_MSG_COMPLETE 1u
#define LCD_MSG_ERROR 0x80u
static uint32_t lcd_scheme_of(void)
{
    uint32_t s = (set.lcd & LCD_SCHEME_MASK) >> LCD_SCHEME_SHIFT;
    return s < LCD_SCHEMES ? s : 0u;
}

static uint32_t text_len(const char *s)       /* (the device's str_len is not in the host or web build) */
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}
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
    ui.lcd_msg = 0;
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

static void program_step(int32_t to)         /* to: the patch over every bank, bank * 64 + program (the encoders walk them all) */
{
    uint32_t per = d50_programs(), banks = d50_banks() ? d50_banks() : 1u, n = per * banks, i;
    i = (uint32_t)(((to % (int32_t)n) + (int32_t)n) % (int32_t)n);
    set.bank = (uint8_t)(i / per);
    set.program = (uint8_t)(i % per);
    set_dirty = 1;
    d50_bank(set.bank);
    d50_program(set.program);
    ui.redraw = 1;
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k;
    int32_t e;
    touch_poll();                                /* the sensed keys' presses and velocities (lib/touch.h) */
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {                /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k, n = (uint32_t)(53 + (int32_t)k + 12 * set.octave);
        if ((keys & bit) && !(ui.keys & bit))
            note(n, touch_vel(k));               /* (1..127; the top row's through the LED probe, else 100) */
        else if (!(keys & bit) && (ui.keys & bit))
            note(n, 0u);
    }
    {                                            /* the keys' pressure as the D-50's channel aftertouch: the hardest-pressed held key
                                                  * (lib/touch.h touch_pressure, 0 at rest; the top row has none). Sent when it moves. */
        uint32_t p = 0;
        for (k = 0; k < NKEYS; k++)
            if ((keys & 1u << k) && touch_pressure(k) > p)
                p = touch_pressure(k);
        if (p != ui.pressure) {
            uint32_t f = plat_lock();
            d50_pressure(p);
            plat_unlock(f);
            ui.pressure = (uint8_t)p;
        }
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
        } else if (k == B_ARP) {                 /* the D-50's CHASE button: Chase Play on or off (d50.h, issue #18) */
            uint32_t f = plat_lock(), on = !d50_chase_on();
            d50_cc(D50_CHASE_CC, on ? 127u : 0u);
            plat_unlock(f);
            app_say("CHASE", on ? "ON" : "OFF");
        } else if (k == B_EDIT && bound) {       /* the edit pages in turn, then the play screen (#31) */
            ui.page = (uint8_t)((ui.page + 1u) % LCD_PAGES);
        } else if (k == B_HOME) {                /* (on its release, or after HOME_LONG_MS held: below) */
            ui.home_ms = plat_ms();
            ui.home_long = 0;
        } else if (k == B_SAVE) {
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SAVED", "");
                ui.lcd_msg = LCD_MSG_COMPLETE;   /* (the LCD says it as the D-50 after WRITE) */
            } else {
                app_say("SAVE", "FAILED");
            }
        }
        ui.redraw = 1;
    }
    /* HOME (issues #23, #26): a press steps through the views, this screen's, the D-50's LCD at true size, folded; held HOME_LONG_MS it
     * steps through the LCD's colours instead */
    if ((btn & 1u << B_HOME) && !ui.home_long && plat_ms() - ui.home_ms >= HOME_LONG_MS) {
        ui.home_long = 1;
        set.lcd = (uint8_t)((set.lcd & ~LCD_SCHEME_MASK) | ((lcd_scheme_of() + 1u) % LCD_SCHEMES) << LCD_SCHEME_SHIFT);
        set_dirty = 1;
        app_say("LCD", lcd_scheme_name[lcd_scheme_of()]);
    } else if (!(btn & 1u << B_HOME) && (ui.btn & 1u << B_HOME) && !ui.home_long) {
        uint32_t v = set.lcd & LCD_TRUE ? LCD_FOLDED : set.lcd & LCD_FOLDED ? 0u : LCD_TRUE;
        set.lcd = (uint8_t)((set.lcd & ~LCD_SHOWN) | v);
        set_dirty = 1;
        ui.redraw = 1;
    }
    if (ui.page != LCD_PAGE_PLAY) {              /* K1..K4: an edit page's parameters, left to right */
        static const uint8_t knob[4] = {EN_K1, EN_K2, EN_K3, EN_K4};
        for (k = 0; k < 4u; k++)
            if ((e = plat_enc(knob[k])) != 0)
                page_edit(k, e);
    } else if ((e = plat_enc(EN_K1)) != 0 && (set.lcd & LCD_TRUE)) {   /* K1: the true-size LCD's dot height, on the play screen */
        set.lcd = (uint8_t)((set.lcd & ~7u) | (uint32_t)clamp((int32_t)lcd_pitch() + (e > 0 ? 1 : -1), LCD_PITCH_MIN, LCD_PITCH_MAX));
        set_dirty = 1;
        ui.redraw = 1;
    }
    if ((e = plat_enc(EN_SELECT)) != 0 || (e = plat_enc(EN_PRESET)) != 0)
        program_step((int32_t)(set.bank * d50_programs() + set.program) + e);
    if (program_asked >= 0) {
        int32_t p = program_asked, b = bank_asked >= 0 ? bank_asked : (int32_t)set.bank;
        program_asked = -1;
        if (b >= (int32_t)(d50_banks() ? d50_banks() : 1u))
            b = set.bank;                        /* (a bank there is not: the current one) */
        program_step(b * (int32_t)d50_programs() + p % (int32_t)d50_programs());
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
    if (now < ui.msg_until && !(bound && (set.lcd & LCD_SHOWN))) {   /* (with the LCD, the message is in it) */
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        cv_text(10, 6, &FONT_B, "D-50", C_WHITE);
        cv_text(70, 10, &FONT_XS, "fm1-base", C_DIM);
        if ((set.lcd & LCD_SHOWN) && d50_banks() > 1u) {   /* (the LCD says I or C: which card is this screen's to say) */
            char b[8] = "BANK  ";
            b[5] = (char)('1' + set.bank);
            cv_text(120, 10, &FONT_XS, b, C_GRAY);
        }
        if (d50_chase_on())
            cv_text(180, 10, &FONT_XS, "CHASE", C_HI);
    }
    cv_blit(0, 0);
    cv_begin(240, 120, C_BLACK);
    if (!bound) {
        cv_text(10, 20, &FONT_M, "NO D-50 DATA", C_HI);
    } else if ((set.lcd & LCD_SHOWN) || ui.page != LCD_PAGE_PLAY) {   /* the D-50's play screen or an edit page (the FM-1 view: folded);
                                                                       * bank 1 the internal memory, the others cards */
        uint32_t pitch = lcd_pitch();
        const uint8_t *pb = d50_patch_bytes();
        lcd_scheme = lcd_scheme_of();
        if (!pb)
            lcd_clear();
        else if (ui.page != LCD_PAGE_PLAY)
            lcd_page_screen(ui.page, set.bank ? 'C' : 'I', p, pb);
        else
            lcd_play_screen(set.bank ? 'C' : 'I', p, pb);
        if (now < ui.msg_until) {
            if (ui.lcd_msg & LCD_MSG_ERROR)
                lcd_error_screen(ui.lcd_msg & ~LCD_MSG_ERROR);
            else if (ui.lcd_msg == LCD_MSG_COMPLETE)
                lcd_complete_screen();
            else
                lcd_message(ui.msg[0], ui.msg[1]);
        }
        if (set.lcd & LCD_TRUE)
            lcd_draw((int32_t)(120u - lcd_height(pitch)) / 2, pitch);
        else
            lcd_draw_folded((int32_t)(120u - LCD_FOLDED_HEIGHT) / 2);
    } else {
        {                                    /* the D-50's 11..88; with several banks "2-37" (issue #7) */
            uint32_t j = 0;
            if (d50_banks() > 1u) {
                t[j++] = (char)('1' + set.bank);
                t[j++] = '-';
            }
            t[j++] = (char)('1' + p / 8u);
            t[j++] = (char)('1' + p % 8u);
            t[j] = 0;
        }
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
    uint_text(plat_cpu_pct(), t + text_len(t));
    text_cat(t, "%  ");
    uint_text(d50_cores(), t + text_len(t));
    text_cat(t, d50_cores() > 1u ? " CORES" : " CORE");
    cv_text(10, 4, &FONT_XS, t, C_GRAY);
    t[0] = 0;
    text_cat(t, "OCT ");
    if (set.octave < 0)
        text_cat(t, "-");
    uint_text((uint32_t)(set.octave < 0 ? -set.octave : set.octave), t + text_len(t));
    text_cat(t, "  LIMIT ");
    uint_text(d50_voice_limit(), t + text_len(t));
    text_cat(t, d50_reverb() ? "  REVERB" : "");
    cv_text(10, 20, &FONT_XS, t, C_GRAY);
    cv_blit(0, 180);
}

void app_ui_init(void)
{
    touch_on(1);                                 /* velocity: the keys' readings on */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_ms, last_midi;
    uint32_t now = plat_ms();
    input();
    if (d50_edit_poll())                         /* a SysEx edit of the current patch: the sound, and its name on the screen */
        ui.redraw = 1;
    {                                            /* a D-50 SysEx frame refused: the D-50's error on the screen (#25) */
        uint32_t fault = d50_sysex_error();
        if (fault) {
            app_say("ERROR", lcd_error_lines[fault < 8u ? fault : 0u]);
            ui.lcd_msg = (uint8_t)(LCD_MSG_ERROR | fault);
            ui.msg_until = now + 2500u;
        }
    }
    while (plat_sysex_room() >= D50_SYSEX_REPLY_MAX) {   /* an RQ1's answer, a DT1 at a time as the computer reads */
        static uint8_t f[D50_SYSEX_REPLY_MAX];
        uint32_t len = d50_sysex_reply(f);
        if (!len)
            break;
        (void)plat_sysex_send(f, len);
    }
    if (plat_cpu_pct() > cpu_peak)
        cpu_peak = plat_cpu_pct();
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
