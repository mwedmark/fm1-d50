/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The D-50 on fm1-base: the panel, MIDI in, and the engine's 32 kHz stereo resampled to the platform's 44.1 kHz. The engine itself is
 * d50/ (d50.h, d50.cpp), compiled in d50_unit.cpp. Part of the platform's unity build (device/unity.c), after
 * the platform: gfx.c and announce.c are in scope. FM1-BASE.md. */
#include "../d50/d50.h"

#define SET_MAGIC 0x44353031u            /* "D501": the settings record */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t program;                     /* 0 .. 63, in the bank */
    uint8_t bank;                        /* 0 .. d50_banks() - 1 (issue #7; was a reserved byte, 0) */
    uint8_t lcd;                         /* the D-50's display (issues #23, #26; was a reserved byte, 0): the view, the low bits its dot pitch */
    uint8_t midi;                        /* the D-50's MIDI (issue #51; a record without it reads 0): the basic channel, and MIDI_OMNI_OFF */
} settings_t;
#define SET_OLD_BYTES 8u                 /* the record before midi (one without it loads, with midi 0: channel 1, omni on) */
#define MIDI_CHANNEL 0x0Fu               /* the basic channel, 0 .. 15 (1 .. 16 on the screen): also the SysEx device ID, as the D-50's */
#define MIDI_OMNI_OFF 0x10u              /* omni off: only the basic channel plays. Omni on (the default): every channel, as before #51 */
#define MIDI_CONTROL_SHIFT 5u            /* the D-50's MIDI-1 Control (system byte 14, #60): the control channel in mono, 0 in an older record */
#define MIDI_CONTROL_MASK (3u << MIDI_CONTROL_SHIFT)
#define MIDI_LOCAL_OFF 0x80u             /* the D-50's Local off (system byte 12, #67): the keys only sent on TxCH. Local on (0 in an older record) */
enum { MIDI_CONTROL_BCH, MIDI_CONTROL_GCH, MIDI_CONTROL_MDEOFF };   /* the basic channel; the global one, below it; mode messages off */
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
static uint32_t midi_control(void)
{
    uint32_t c = (set.midi & MIDI_CONTROL_MASK) >> MIDI_CONTROL_SHIFT;
    return c > MIDI_CONTROL_MDEOFF ? MIDI_CONTROL_BCH : c;
}

/* ------------------------------------------------------------------ the sound --- */
/* The engine renders at 32 kHz; a block of APP_BLOCK output frames takes 92 or 93 of its frames, resampled here
 * (d50/resample_table.inc from tools/make_resampler.js): 16-bit samples, one array a channel, after the last RESAMPLE_TAPS
 * frames before them; position is where output frame 0 of the next block sits, in frames, Q24. Silence skips the filter. */
#include "../d50/resample_table.inc"
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

/* The joystick's balances over MIDI (ours, as Chase Play's CC80: the D-50 has no controller for them), 0..127 over 0..100 (#58) */
#define D50_BALANCE_CC 81u
#define D50_PARTIAL_BALANCE_CC 82u
static volatile int8_t joy_asked[2] = {-1, -1};
static volatile int8_t omni_asked = -1;      /* CC 124 (0, omni off) or 125 (1, on), from midi_in (#59) */
static volatile int8_t local_asked = -1;     /* CC 122: 0 Local off, 1 on, from midi_in (#67) */
/* Active Sensing (#68), as the D-50's (0x4AE1, 0x4AFC, 0x2741, 0x574F): any message zeroes the time, an FE arms the watch, and 43 of the
 * firmware's ticks without one (16 ms assumed: about 0.69 s) end the notes and set the controllers back (d50_sensing_lost); disarmed
 * until the next FE */
#define SENSING_MS 688u
static volatile uint32_t sensing_ms;         /* plat_ms of the last message in */
static volatile uint8_t sensing_on;          /* an FE came since the last loss */
/* The D-50's mono mode (#60, docs/D50-FIRMWARE.md): MIDI's state, not kept (the D-50's is in RAM: power-on is neither mono nor poly) */
static volatile uint8_t mono_m;              /* mono's channel count, 1..8 (CC 126); 0 not mono */
static volatile uint8_t poly_rx;             /* a Poly (CC 127) came: the Solo key modes play polyphonically */
static volatile uint8_t mode_said;           /* a mode message for the main loop to show */
static void engine_mode(void)                /* the engine's mode from them and omni (the firmware's rows at 0x69EC): mono is mode 4 with omni off */
{
    if (mono_m && (set.midi & MIDI_OMNI_OFF))
        d50_mode(D50_MODE_MONO, mono_m);
    else
        d50_mode(mono_m || poly_rx ? D50_MODE_POLY : D50_MODE_NORMAL, 0);
}
static void midi_in(void)                    /* audio interrupt: the packets that came in */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, part = 0, play = 1, control = 1;
        sensing_ms = plat_ms();                  /* (any message, the clock too: 0x4AE1) */
        if (st == 0xFEu)
            sensing_on = 1;                      /* Active Sensing: the watch armed (0x4AFC, #68) */
        if (st == 0xF8u || st == 0xFEu)
            continue;
        if (st < 0xF0u) {                        /* the D-50's channel test (0x4B81, #51, #60): the channel less the basic channel is the part */
            const uint32_t basic = set.midi & MIDI_CHANNEL, sc = d50_sep_channel(), t = st & 0xF0u;
            if (sc && (st & 0x0Fu) == sc - 1u && t != 0xA0u && t != 0xC0u)
                d50_sep(st, d1, d2);             /* SEP, SEP-S: the patch's SepCH plays the Upper (0x5DB7, #62). As on the D-50 the message also
                                                  * goes through the test below: on the basic channel, or with omni on, it plays the Lower too */
            if (set.midi & MIDI_OMNI_OFF) {      /* (omni on: every channel is the basic channel's, part 0) */
                const uint32_t last = mono_m ? mono_m - 1u : 0u;
                part = ((st & 0x0Fu) - basic) & 0x0Fu;
                play = part <= last;             /* notes and the bender: the mono parts, or the basic channel */
                control = part == (mono_m && midi_control() == MIDI_CONTROL_GCH ? 15u : 0u);   /* the rest: Control's channel in mono */
            }
            if (!play && !control)
                continue;
        }
        midi_count++;
        switch (st & 0xF0u) {
        case 0x90u: if (play) d50_note_ch(part, d1, d2); break;
        case 0x80u: if (play) d50_note_ch(part, d1, 0); break;
        case 0xE0u: if (play) d50_bend_ch(part, d1 | d2 << 7); break;
        case 0xC0u: if (control) program_asked = (int32_t)d1; break;
        case 0xB0u:
            if (d1 == 120u || d1 == 123u) {      /* all sound off, all notes off: ours, a panic on any channel it plays (the D-50 ignores 120 and,
                                                  * with omni on, 123) */
                d50_panic();
            } else if (d1 >= 122u) {             /* the mode messages: on the basic channel only (0x4D3F), each ending with the notes off */
                if ((st & 0x0Fu) != (set.midi & MIDI_CHANNEL))
                    break;
                if ((d1 == 124u || d1 == 125u) && d2 == 0u) {
                    d50_panic();                 /* Omni Off/On, the D-50's (0x4F5A, 0x4F39; #59): value 0, notes off */
                    omni_asked = (int8_t)(d1 - 124u);   /* the main loop sets omni, and the engine's mode with it */
                } else if (d1 == 126u && d2 <= 16u && midi_control() != MIDI_CONTROL_MDEOFF) {
                    uint32_t m = d2 == 0u || d2 > 8u ? 8u : d2, basic = set.midi & MIDI_CHANNEL;   /* Mono (0x4EDF, 0x4F15) */
                    if (basic + m > 16u)
                        m = 16u - basic;         /* (no further than channel 16) */
                    mono_m = (uint8_t)m;
                    poly_rx = 0;
                    engine_mode();
                    if (midi_control() == MIDI_CONTROL_GCH && (set.midi & MIDI_OMNI_OFF))
                        d50_rpn_reset();         /* (0x4F7E: with the global channel the RPN starts again, #66) */
                    mode_said = 1;
                } else if (d1 == 127u && d2 == 0u && midi_control() != MIDI_CONTROL_MDEOFF) {
                    mono_m = 0;                  /* Poly (0x4EAC) */
                    poly_rx = 1;
                    engine_mode();
                    mode_said = 1;
                }
                if (d1 == 122u && (d2 == 0u || d2 == 127u))
                    local_asked = (int8_t)(d2 ? 1 : 0);   /* Local Control (0x5F20, #67): 0 off, 127 on, others ignored; the main loop */
                break;
            }
            else if (!control)
                break;
            else if (d1 == D50_BALANCE_CC || d1 == D50_PARTIAL_BALANCE_CC)
                joy_asked[d1 - D50_BALANCE_CC] = (int8_t)d2;   /* the joystick's two axes, absolute (#58); the main loop does it */
            else if (d1 == 0u)
                bank_asked = (int32_t)d2;        /* (not the D-50's: its banks were cards; upstream PicoFaceD5's) */
            else
                d50_cc(d1, d2);                  /* the D-50's controls: d50.h d50_cc (issue #4) */
            break;
        case 0xD0u: if (control) d50_pressure(d1); break;   /* channel pressure: the D-50's aftertouch */
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
/* cmd 11 and 12 (the frames of the first build, on M-VAVE's SDK):
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
    int n;
    memset(&set, 0, sizeof set);
    n = plat_store_load(OBJ_SETTINGS, &set, sizeof set);
    if ((n != (int)sizeof set && n != (int)SET_OLD_BYTES) || set.magic != SET_MAGIC || set.octave < -2 || set.octave > 2 ||
        set.program > 63u) {
        memset(&set, 0, sizeof set);
        set.magic = SET_MAGIC;
    }
    set.midi &= MIDI_CHANNEL | MIDI_OMNI_OFF | MIDI_CONTROL_MASK | MIDI_LOCAL_OFF;   /* (Control was masked away before: #60, #67) */
    d50_device(set.midi & MIDI_CHANNEL);         /* the SysEx device ID: the basic channel's (issue #51) */
}

static uint32_t lcd_pitch(void)
{
    uint32_t p = set.lcd & 7u;
    return p < LCD_PITCH_MIN || p > LCD_PITCH_MAX ? LCD_PITCH_DEFAULT : p;
}

/* The D-50's data (d50.cpp fm1_flash_map): the resource region's payload. The FM-1's firmware has the region with resources_var (fm1.toml:
 * any data the host writes, the install page's own file; GitHub's build never holds Roland's data: issues #36, #47), so plat_res gives it
 * on the device. The browser emulator's data area is not plat_res's (the data you load: web/emu/app.js), so there the region is read as it
 * lies: the commit record (device/res.c: "RES1", the payload's length and CRC-32, the record's own CRC) and the payload checked against
 * it once. d50.cpp then checks its own header ("FMD5"). An update from GitHub keeps the data the FM-1 has and plays it. */
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
const uint8_t *app_d50_data(uint32_t *len)
{
#if FM1B_RES
    {
        uint32_t n = 0;
        const uint8_t *p = plat_res(&n);
        if (p && n) {
            if (len)
                *len = n;
            return p;
        }
    }
#endif
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
}

/* What the region holds, as its commit record says (the payload's CRC-32; 0 while it holds nothing): the data the engine was bound to
 * at the start. New data written over USB (the install page, tools/fm1_resources.py) changes it, and the engine is bound once: a restart
 * binds it (app_frame). */
static uint32_t res_stamp(void)
{
    uint32_t n = 0, crc = 0;
    const uint8_t *r = plat_data(APP_RES_BASE);
#if FM1B_RES
    (void)plat_res(&n);
#endif
    if (r && n)
        memcpy(&crc, r + 8, sizeof crc);
    return crc;
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
    int8_t tune_off;                            /* Master Tune 0..100 (#54) less 50 (so 0 = 440.0 Hz at start); not kept over a power cycle */
    uint8_t tx_program;                         /* with Local off, the program change the encoders send (#67), 0..63 */
    int8_t transpose;                            /* Key Transpose -12..12 (#54) */
    uint8_t lower;                               /* the selected tone, the joystick's X: 0 the upper, 1 the lower (#58) */
    uint8_t page;                                /* the edit page (#31): d50_lcd.h LCD_PAGE_*, LCD_PAGE_PLAY none */
} ui;
#include "d50_lcd.h"
/* A byte of the temporary area (the current patch, 7 blocks of 64: the upper's partial 1, partial 2 and common, the lower's, the patch block)
 * changed by step within lo..hi: a DT1 (d50_sysex, #6), so the sound follows. Returns the new value, -1 where there is no patch. */
static int32_t patch_edit(uint32_t at, int32_t step, int32_t lo, int32_t hi)
{
    const uint8_t *p = d50_patch_bytes();
    uint8_t f[9] = {0x41, 0, 0x14, 0x12, 0, 0, 0, 0, 0};       /* 41 dev 14 12, the address (three 7-bit bytes), the value, the sum */
    int32_t v;
    f[1] = (uint8_t)(set.midi & MIDI_CHANNEL);   /* (the device ID d50_sysex takes: issue #51) */
    if (!p)
        return -1;
    v = clamp((int32_t)p[at] + step, lo, hi);
    f[4] = (uint8_t)(at >> 14 & 127u);
    f[5] = (uint8_t)(at >> 7 & 127u);
    f[6] = (uint8_t)(at & 127u);
    f[7] = (uint8_t)v;
    f[8] = (uint8_t)((128u - ((f[4] + f[5] + f[6] + f[7]) & 127u)) & 127u);
    (void)d50_sysex(f, sizeof f);
    ui.redraw = 1;
    return v;
}
/* The MIDI page's values (issue #51, lcd_pages' own): 0 the basic channel (0..15), 1 omni (1 on), 2 Control (#60), 3 Local (1 on, #67) */
static void midi_values(uint8_t *v)
{
    v[0] = (uint8_t)(set.midi & MIDI_CHANNEL);
    v[1] = (uint8_t)(set.midi & MIDI_OMNI_OFF ? 0u : 1u);
    v[2] = (uint8_t)midi_control();
    v[3] = (uint8_t)(set.midi & MIDI_LOCAL_OFF ? 0u : 1u);
}
static void local_keys_off(void);
/* The MIDI settings changed (the page, or CC 124/125), main loop: the device ID, and the mode as the D-50 has it (#60). Control set to
 * MdeOff returns to the power-on state (0x5829); mono's channels end at channel 16 (0x4F15). */
static void midi_changed(uint32_t old)
{
    uint32_t f, m;
    d50_device(set.midi & MIDI_CHANNEL);
    if (set.midi == old)
        return;
    if ((set.midi & MIDI_LOCAL_OFF) && !(old & MIDI_LOCAL_OFF))
        local_keys_off();                        /* Local off: the keys' notes end (0x5773, #67) */
    f = plat_lock();
    if ((set.midi ^ old) & MIDI_CHANNEL)
        d50_rpn_reset();                         /* a basic channel change: the RPN null (0x5BEC, #66) */
    m = mono_m;
    if ((set.midi & MIDI_CONTROL_MASK) != (old & MIDI_CONTROL_MASK) && midi_control() == MIDI_CONTROL_MDEOFF) {
        m = 0xFFu;                               /* (the notes off even when nothing was mono or poly) */
        mono_m = poly_rx = 0;
    }
    if (mono_m && (set.midi & MIDI_CHANNEL) + mono_m > 16u)
        mono_m = (uint8_t)(16u - (set.midi & MIDI_CHANNEL));
    if (((set.midi ^ old) & MIDI_OMNI_OFF) || mono_m != m)
        engine_mode();                           /* (the notes off, as a mode change does on the D-50) */
    plat_unlock(f);
}
/* A parameter changed on an edit page: the patch block's (#31), or the MIDI page's settings (#51; kept with SAVE) */
static void page_edit(uint32_t field, int32_t step)
{
    const lcd_page_t *g = &lcd_pages[ui.page];
    if (field >= g->fields)
        return;
    if (g->own) {
        uint8_t v[4];
        const uint32_t old = set.midi;
        midi_values(v);
        v[field] = (uint8_t)clamp((int32_t)v[field] + (step > 0 ? 1 : -1), g->f[field].lo, g->f[field].hi);
        set.midi = (uint8_t)((v[0] & MIDI_CHANNEL) | (v[1] ? 0u : MIDI_OMNI_OFF) | (uint32_t)v[2] << MIDI_CONTROL_SHIFT |
                             (v[3] ? 0u : MIDI_LOCAL_OFF));
        midi_changed(old);
        set_dirty = 1;
        ui.redraw = 1;
        return;
    }
    (void)patch_edit(6u * 64u + g->f[field].byte, step, g->f[field].lo, g->f[field].hi);
}
static void uint_text(uint32_t v, char *b);
/* The D-50's joystick on the play screen (#43), as its firmware has it (docs/D50-FIRMWARE.md, #58): K1 the stick's X, the balance between
 * the partials of the selected tone (that tone's common byte 47, parameter 0x66), K2 its Y, the balance between the upper and the lower
 * tone (the patch block's byte 33, parameter 0x8A, what the LCD shows as Bal). K4 selects the tone, as the D-50's UPPER and LOWER keys
 * (the upper at start). Edits of the current patch, as the edit pages' are: they last until another program is chosen, and SysEx reads
 * them back. */
enum { PATCH_COMMON_UPPER = 2u * 64u, PATCH_COMMON_LOWER = 5u * 64u, COMMON_PARTIAL_BALANCE = 47u, PATCH_TONE_BALANCE = 6u * 64u + 33u };
enum { JOY_X, JOY_Y };
static uint32_t joystick_byte(uint32_t axis)
{
    if (axis == JOY_Y)
        return PATCH_TONE_BALANCE;
    return (ui.lower ? PATCH_COMMON_LOWER : PATCH_COMMON_UPPER) + COMMON_PARTIAL_BALANCE;
}
static void joystick(uint32_t axis, int32_t step)
{
    char t[12];
    int32_t v = patch_edit(joystick_byte(axis), step, 0, 100);
    if (v < 0)
        return;
    if (axis == JOY_Y && (set.lcd & LCD_SHOWN))  /* (the LCD shows the tone balance itself, as Bal; the partial balance it has no place for) */
        return;
    uint_text((uint32_t)v, t);
    app_say(axis == JOY_Y ? "TONE BAL" : ui.lower ? "L PART BAL" : "U PART BAL", t);
}
static void joystick_to(uint32_t axis, int32_t v127)      /* a MIDI value of the axis: 0..127 over 0..100 */
{
    const uint8_t *p = d50_patch_bytes();
    int32_t d;
    if (!p)
        return;
    d = (v127 * 100 + 63) / 127 - (int32_t)p[joystick_byte(axis)];
    if (d != 0)
        joystick(axis, d);
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

/* MIDI out (#65), what the D-50 sends on TxCH (docs/D50-FIRMWARE.md): the patch's byte 37, B (0: the basic channel) or 1..16. The
 * FM-1's keys (0x5FA0, 0x603D: moved by Key Transpose, a note-off as velocity 0, CC 123 All Notes Off when the last is released), their
 * aftertouch (0x3232), and a patch chosen with the encoders (0x551B; not one MIDI asked for). On a TxCH change (0x58AA) the held keys'
 * note-offs and All Notes Off go out on the old channel, then CC 124 Omni Off and CC 127 Poly on the new (0x56FA). Over USB, main loop. */
static uint8_t tx_ch = 0xFFu;                    /* the channel the last went out on, 0..15; 0xFF before the first look */
static uint8_t tx_key[128];                      /* a key's sent note + 1, for its note-off; 0: none out */
static void tx(uint32_t st, uint32_t d1, uint32_t d2)   /* a USB-MIDI packet, cable 0: CIN, status, data */
{
    plat_midi_out((st >> 4 & 15u) | (st & 0xFFu) << 8 | (d1 & 127u) << 16 | (d2 & 127u) << 24);
}
static uint32_t tx_keys_out(void)
{
    uint32_t n, c = 0;
    for (n = 0; n < 128u; n++)
        c += tx_key[n] ? 1u : 0u;
    return c;
}
static void tx_check(void)                       /* TxCH from the current patch, and its change */
{
    const uint8_t *p = d50_patch_bytes();
    uint32_t t = p ? p[6u * 64u + 37u] : 0u, n;
    t = t >= 1u && t <= 16u ? t - 1u : (set.midi & MIDI_CHANNEL);
    if (tx_ch == 0xFFu) {                        /* (the first: nothing to change from) */
        tx_ch = (uint8_t)t;
        ui.tx_program = set.program;             /* (Local off: the encoders' program changes start here, #67) */
        return;
    }
    if (t == tx_ch)
        return;
    if (tx_keys_out()) {                         /* the held keys end on the old channel (0x61D6) */
        for (n = 0; n < 128u; n++)
            if (tx_key[n]) {
                tx(0x90u | tx_ch, tx_key[n] - 1u, 0);
                tx_key[n] = 0;
            }
        tx(0xB0u | tx_ch, 123u, 0);
    }
    tx_ch = (uint8_t)t;
    tx(0xB0u | tx_ch, 124u, 0);                  /* Omni Off, Poly: a receiver on it plays in mode 3 */
    tx(0xB0u | tx_ch, 127u, 0);
}
static void tx_note(uint32_t n, uint32_t vel)
{
    if (vel) {
        const int32_t s = clamp((int32_t)n + ui.transpose, 0, 127);
        if (tx_key[n])                           /* (struck again before its note-off) */
            tx(0x90u | tx_ch, tx_key[n] - 1u, 0);
        tx_key[n] = (uint8_t)(s + 1);
        tx(0x90u | tx_ch, (uint32_t)s, vel);
    } else if (tx_key[n]) {
        tx(0x90u | tx_ch, tx_key[n] - 1u, 0);
        tx_key[n] = 0;
        if (!tx_keys_out())
            tx(0xB0u | tx_ch, 123u, 0);          /* the last key up: All Notes Off (0x602D) */
    }
}

/* Local (#67, the D-50's system byte 12): with it off the keys are only sent (0x5FB4). local_key: the keys whose note sounds here, so that
 * a key pressed with Local on ends its note when it is lifted after Local went off, and Local off ends them all at once (0x5773). */
static uint8_t local_key[128];
static uint32_t local_on(void) { return !(set.midi & MIDI_LOCAL_OFF); }
static void local_keys_off(void)
{
    uint32_t n, f = plat_lock();
    for (n = 0; n < 128u; n++)
        if (local_key[n]) {
            d50_key(n, 0);
            local_key[n] = 0;
        }
    d50_pressure(0);
    plat_unlock(f);
    ui.tx_program = set.program;                 /* (the encoders' program changes start from the patch that sounds) */
}
static void note(uint32_t note, uint32_t vel)    /* main loop: the queue is the interrupt's too. The FM-1's keys: Key Transpose moves them (#64) */
{
    if (vel ? local_on() : local_key[note]) {
        uint32_t f = plat_lock();
        d50_key(note, vel);
        plat_unlock(f);
        local_key[note] = (uint8_t)(vel != 0);
    }
    tx_note(note, vel);                          /* and on TxCH (#65), whatever Local is */
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
    tx_check();                                  /* MIDI out's channel, TxCH (#65) */
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
            if (local_on()) {                    /* (Local off: only sent, #67) */
                uint32_t f = plat_lock();
                d50_pressure(p);
                plat_unlock(f);
            }
            tx(0xD0u | tx_ch, p, 0);             /* and on TxCH (#65, 0x3232) */
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
            ui.page = ui.page == LCD_PAGE_TUNE ? LCD_PAGE_PLAY : (uint8_t)((ui.page + 1u) % LCD_PAGES);
        } else if (k == B_SEL && bound) {        /* the TUNE/FUNCTION page, and back to the play screen (#54) */
            ui.page = ui.page == LCD_PAGE_TUNE ? LCD_PAGE_PLAY : LCD_PAGE_TUNE;
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
    for (k = 0; k < 2u; k++) {                   /* CC81 the tone balance (Y), CC82 the selected tone's partial balance (X) (#58) */
        int32_t v = joy_asked[k];
        if (v >= 0) {
            joy_asked[k] = -1;
            joystick_to(k == 0u ? JOY_Y : JOY_X, v);
        }
    }
    {                                            /* CC 124/125: the MIDI Channel page's Omni, as the D-50's MIDI-1 page (#59) */
        int32_t o = omni_asked;
        if (o >= 0) {
            omni_asked = -1;
            if ((uint32_t)o != ((set.midi & MIDI_OMNI_OFF) ? 0u : 1u)) {
                const uint32_t old = set.midi;
                set.midi = (uint8_t)(o ? set.midi & ~MIDI_OMNI_OFF : set.midi | MIDI_OMNI_OFF);
                midi_changed(old);               /* (mono plays as mode 4 only with omni off: #60) */
                set_dirty = 1;
                ui.redraw = 1;
            }
            app_say("OMNI", o ? "ON" : "OFF");
        }
    }
    {                                            /* Master Tune set by RPN 1 (#66): the TUNE/FUNCTION page follows */
        static unsigned seen;
        const unsigned v = d50_midi_tune();
        if (v >> 8 != seen >> 8) {
            seen = v;
            ui.tune_off = (int8_t)((int32_t)(v & 255u) - 50);
            ui.redraw = 1;
        }
    }
    {                                            /* Active Sensing lost (#68): the sender or the cable is gone */
        const uint32_t last = sensing_ms;        /* (read before the time: a message landing between them cannot look 4 billion ms old) */
        if (sensing_on && (int32_t)(plat_ms() - last) >= (int32_t)SENSING_MS) {
            uint32_t f = plat_lock(), lost = sensing_on && sensing_ms == last;
            if (lost) {
                sensing_on = 0;
                d50_sensing_lost();
            }
            plat_unlock(f);
            if (lost)
                app_say("MIDI", "SENSING LOST");
        }
    }
    {                                            /* CC 122: the MIDI Channel page's Local, as the D-50's MIDI-1 page (#67) */
        int32_t l = local_asked;
        if (l >= 0) {
            local_asked = -1;
            if ((uint32_t)l != local_on()) {
                const uint32_t old = set.midi;
                set.midi = (uint8_t)(l ? set.midi & ~MIDI_LOCAL_OFF : set.midi | MIDI_LOCAL_OFF);
                midi_changed(old);
                set_dirty = 1;
                ui.redraw = 1;
            }
            app_say("LOCAL", l ? "ON" : "OFF");
        }
    }
    if (mode_said) {                             /* CC 126/127 (#60) */
        char t[8] = "POLY";
        mode_said = 0;
        if (mono_m) {
            t[0] = (char)('0' + mono_m);
            t[1] = ' ';
            t[2] = 'C';
            t[3] = 'H';
            t[4] = 0;
        }
        app_say(mono_m ? "MONO" : "MIDI", t);
        ui.redraw = 1;
    }
    if (ui.page == LCD_PAGE_TUNE) {              /* K1 Master Tune, K2 Key Transpose (#54) */
        if ((e = plat_enc(EN_K1)) != 0) {
            ui.tune_off = (int8_t)clamp((int32_t)ui.tune_off + e, -50, 50);
            d50_tune((unsigned)(50 + ui.tune_off));
            ui.redraw = 1;
        }
        if ((e = plat_enc(EN_K2)) != 0) {
            ui.transpose = (int8_t)clamp((int32_t)ui.transpose + e, -12, 12);
            d50_transpose(ui.transpose);
            ui.redraw = 1;
        }
    } else if (ui.page != LCD_PAGE_PLAY) {       /* K1..K4: an edit page's parameters, left to right */
        static const uint8_t knob[4] = {EN_K1, EN_K2, EN_K3, EN_K4};
        for (k = 0; k < 4u; k++)
            if ((e = plat_enc(knob[k])) != 0)
                page_edit(k, e);
    } else {                                     /* the play screen: K1 and K2 the joystick's X and Y (#43, #58), K3 the dot height of the
                                                  * true-size LCD (was K1), K4 the selected tone (right the lower, left the upper) */
        if ((e = plat_enc(EN_K1)) != 0)
            joystick(JOY_X, e);
        if ((e = plat_enc(EN_K2)) != 0)
            joystick(JOY_Y, e);
        if ((e = plat_enc(EN_K4)) != 0 && (uint32_t)(e > 0) != ui.lower) {
            ui.lower = (uint8_t)(e > 0);
            app_say("TONE", ui.lower ? "LOWER" : "UPPER");
        }
        if ((e = plat_enc(EN_K3)) != 0 && (set.lcd & LCD_TRUE)) {
            set.lcd = (uint8_t)((set.lcd & ~7u) | (uint32_t)clamp((int32_t)lcd_pitch() + (e > 0 ? 1 : -1), LCD_PITCH_MIN, LCD_PITCH_MAX));
            set_dirty = 1;
            ui.redraw = 1;
        }
    }
    if ((e = plat_enc(EN_SELECT)) != 0 || (e = plat_enc(EN_PRESET)) != 0) {
        if (local_on()) {
            program_step((int32_t)(set.bank * d50_programs() + set.program) + e);
            tx_check();                          /* the new patch's TxCH first, then its program change on it (0x25B6, #65) */
            tx(0xC0u | tx_ch, set.program, 0);
            ui.tx_program = set.program;
        } else {                                 /* Local off: only the program change goes out, the sound stays (0x25B6, #67) */
            char t[4];
            ui.tx_program = (uint8_t)(((int32_t)ui.tx_program + e % 64 + 64) % 64);
            tx(0xC0u | tx_ch, ui.tx_program, 0);
            t[0] = (char)('1' + ui.tx_program / 8u);
            t[1] = (char)('1' + ui.tx_program % 8u);
            t[2] = 0;
            app_say("PROG OUT", t);
        }
    }
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
        else if (ui.page == LCD_PAGE_TUNE)
            lcd_tune_screen((uint32_t)(50 + ui.tune_off), ui.transpose);
        else if (ui.page != LCD_PAGE_PLAY) {
            uint8_t own[4];
            midi_values(own);
            lcd_page_screen(ui.page, set.bank ? 'C' : 'I', p, pb, own);
        } else {
            lcd_play_screen(set.bank ? 'C' : 'I', p, pb);
        }
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
    static uint32_t stamp0, restart_at;
    static uint8_t stamped;
    if (!stamped) {
        stamped = 1;
        stamp0 = res_stamp();
    } else if (!restart_at && res_stamp() != stamp0)   /* new D-50 data was written: say so a moment, then start on it */
        restart_at = (now + 2500u) | 1u;
    if (restart_at && (int32_t)(now - restart_at) >= 0)
        plat_restart();
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
