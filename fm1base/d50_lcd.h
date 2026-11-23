/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The D-50's display on the FM-1's screen (issue #23): a 2 x 40 character buffer, drawn as the D-50's backlit LCD of 5 x 8 dot characters.
 * The 240 pixels across hold the 40 columns at 6 pixels a character (5 dots, a pixel between characters); the dots are drawn taller than
 * they are wide (lcd_draw's pitch) so that the two lines can be read. The play screen is the D-50 v1.06 firmware's own layout (the template
 * at 0x6EAC of its EPROM). Included by app.c, after the platform's gfx.c (cv_*). */

enum { LCD_ROWS = 2, LCD_COLS = 40 };
static char lcd_buf[LCD_ROWS][LCD_COLS];

/* The character ROM's glyphs, codes 0x20..0x7F, the rows top to bottom, 5 bits each (bit 4 the left dot); the eighth row is the cursor's,
 * always dark here. The set is the HD44780 A00's, as the D-50's display module's: 0x5C is the yen sign, 0x7E and 0x7F are the arrows the
 * firmware's "Int ~ Card" and "[ <- ]" mean. Drawn for this from the standard dot patterns. */
static const uint8_t lcd_font[96][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x04, 0x04, 0x04, 0x04, 0x00, 0x00, 0x04}, /* space ! */
    {0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00}, {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}, /* " # */
    {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}, {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}, /* $ % */
    {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}, {0x0C, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}, /* & ' */
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}, {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}, /* ( ) */
    {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}, {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}, /* * + */
    {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}, {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, /* , - */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}, {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}, /* . / */
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* 0 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, /* 2 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, /* 4 5 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 6 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, /* 8 9 */
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}, {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08}, /* : ; */
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}, {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}, /* < = */
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}, {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}, /* > ? */
    {0x0E, 0x11, 0x01, 0x0D, 0x15, 0x15, 0x0E}, {0x0E, 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11}, /* @ A */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, /* B C */
    {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, /* D E */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, /* F G */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* H I */
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* J K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, /* L M */
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* N O */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, /* P Q */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* R S */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* T U */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, /* V W */
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}, /* X Y */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}, /* Z [ */
    {0x11, 0x0A, 0x1F, 0x04, 0x1F, 0x04, 0x04}, {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}, /* yen ] */
    {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}, /* ^ _ */
    {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F}, /* ` a */
    {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E}, {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E}, /* b c */
    {0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F}, {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E}, /* d e */
    {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08}, {0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E}, /* f g */
    {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11}, {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E}, /* h i */
    {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C}, {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}, /* j k */
    {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11}, /* l m */
    {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11}, {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E}, /* n o */
    {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10}, {0x00, 0x00, 0x0D, 0x13, 0x0F, 0x01, 0x01}, /* p q */
    {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}, {0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E}, /* r s */
    {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06}, {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D}, /* t u */
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04}, {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A}, /* v w */
    {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11}, {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E}, /* x y */
    {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F}, {0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02}, /* z { */
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, {0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08}, /* | } */
    {0x00, 0x04, 0x02, 0x1F, 0x02, 0x04, 0x00}, {0x00, 0x04, 0x08, 0x1F, 0x08, 0x04, 0x00}, /* right arrow, left arrow */
};

/* The LCD's colours (issue #26), chosen with a long press of HOME: the glass between the characters, a dot that is off, a dot that is on.
 * The amber and the blue are from photographs of D-50s (light dots on a dark backlight, the dots that are off a shade lighter than the
 * glass); the green (dark dots on a yellow-green backlight) is the usual LCD's, not seen on a D-50. (The dot rows touch: with a line
 * between them as well the 1-pixel columns could not be read.) */
enum { LCD_SCHEMES = 3 };
static const char *const lcd_scheme_name[LCD_SCHEMES] = {"GREEN", "AMBER", "BLUE"};
static const uint16_t lcd_colours[LCD_SCHEMES][3] = {
    {RGB(156, 190, 44), RGB(148, 182, 41), RGB(28, 40, 14)},
    {RGB(176, 70, 6), RGB(196, 86, 12), RGB(255, 214, 72)},
    {RGB(52, 62, 178), RGB(76, 92, 210), RGB(236, 240, 255)},
};
static uint32_t lcd_scheme;                      /* 0 .. LCD_SCHEMES - 1 (app.c sets it from the settings) */
#define LCD_GLASS lcd_colours[lcd_scheme][0]
#define LCD_OFF lcd_colours[lcd_scheme][1]
#define LCD_ON lcd_colours[lcd_scheme][2]

static void lcd_clear(void)
{
    uint32_t r, c;
    for (r = 0; r < LCD_ROWS; r++)
        for (c = 0; c < LCD_COLS; c++)
            lcd_buf[r][c] = ' ';
}

static void lcd_put(uint32_t row, uint32_t col, const char *s, uint32_t n)   /* n characters of s (to its end), from row, col */
{
    while (n-- && *s && col < LCD_COLS)
        lcd_buf[row][col++] = *s++;
}

/* The patch's characters (its own set, 0..63) n of them from at, into the buffer */
static void lcd_put_d50(uint32_t row, uint32_t col, const uint8_t *at, uint32_t n)
{
    static const char chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890-";
    while (n-- && col < LCD_COLS) {
        uint8_t c = *at++;
        lcd_buf[row][col++] = c < 64u ? chars[c] : '?';
    }
}

/* The play screen, the firmware's template (0x6EAC):
 *   "$-$$ $$$$$$$$$$$$$$$$$$    U: $$$$$$$$$$"   memory (I or C), the patch number, its name; the upper tone's name
 *   " $$$$$$  SP $$$  Bal=$$    L: $$$$$$$$$$"   the key mode, the split point, the tone balance; the lower tone's name
 * as a real D-50 shows it (a photograph, issue #26): "SP C4" (the note, a sharp, the octave: "C#4"), and "Bal 50", the balance in the
 * three columns from the template's '=' (so 100 fits).
 * memory: 'I' (internal) or 'C' (card); program 0..63; p the patch's 448 bytes. */
static void lcd_play_screen(char memory, uint32_t program, const uint8_t *p)
{
    static const char modes[] = "WHOLE  DUAL SPLIT  SEP  WHOL-SDUAL-SSPL-USSPL-LSSEP-S ";   /* 6 each, key mode 0..8 (0x7C80) */
    static const char notes[] = "C C#D D#E F F#G G#A A#B ";                                 /* 2 each (0x3F35) */
    const uint8_t *pb = p + 6u * 64u;                                                     /* the patch block */
    char t[4];
    uint32_t v;
    lcd_clear();
    t[0] = memory;
    t[1] = '-';
    t[2] = (char)('1' + program / 8u % 8u);
    t[3] = (char)('1' + program % 8u);
    lcd_put(0, 0, t, 4);
    lcd_put_d50(0, 5, pb, 18);
    lcd_put(0, 27, "U: ", 3);
    lcd_put_d50(0, 30, p + 2u * 64u, 10);                                                 /* the upper common's name */
    v = pb[18] < 9u ? pb[18] : 0u;
    lcd_put(1, 1, modes + 6u * v, 6);
    lcd_put(1, 9, "SP ", 3);
    v = 36u + (pb[19] <= 60u ? pb[19] : 60u);                                             /* C2 .. C7 */
    {
        uint32_t n = 0;
        t[n++] = notes[2u * (v % 12u)];
        if (notes[2u * (v % 12u) + 1u] == '#')
            t[n++] = '#';
        t[n++] = (char)('0' + v / 12u - 1u);
        lcd_put(1, 12, t, n);
    }
    lcd_put(1, 17, "Bal", 3);
    v = pb[33] <= 100u ? pb[33] : 100u;                                                   /* 0 .. 100, right-aligned in 20..22 */
    t[0] = v >= 100u ? '1' : ' ';
    t[1] = v >= 10u ? (char)('0' + v / 10u % 10u) : ' ';
    t[2] = (char)('0' + v % 10u);
    lcd_put(1, 20, t, 3);
    lcd_put(1, 27, "L: ", 3);
    lcd_put_d50(1, 30, p + 5u * 64u, 10);                                                 /* the lower common's name */
}

/* Messages (issue #25), the firmware's own 40-character lines (v1.06, 0x6C51 on, and 0x6E84). An error is two lines: "Error  Reject"
 * above, the fault below (d50.h D50_ERR_* index the list). "Complete ." stands alone on the lower line, as after the D-50's WRITE. */
static const char *const lcd_error_lines[8] = {
    "Error  Reject",        "Invalid Patch Number", "Rx FIFO Overflow",    "Invalid Paramater(s)",
    "Invalid Data Address", "Invalid Data Bytes",   "Invalid Data Format", "Sum Check Error",
};
static void lcd_error_screen(uint32_t fault)
{
    lcd_clear();
    lcd_put(0, 0, lcd_error_lines[0], LCD_COLS);
    if (fault < 8u)
        lcd_put(1, 0, lcd_error_lines[fault], LCD_COLS);
}
static void lcd_complete_screen(void)
{
    lcd_clear();
    lcd_put(1, 14, "Complete .", LCD_COLS);
}
/* The panel's own messages (the D-50 has none of them: CHASE ON, LCD AMBER and so on): the lower line, from where "Complete ." stands,
 * over the screen that shows */
static void lcd_message(const char *a, const char *b)
{
    uint32_t c;
    for (c = 0; c < LCD_COLS; c++)
        lcd_buf[1][c] = ' ';
    for (c = 14; *a && c < LCD_COLS; )
        lcd_buf[1][c++] = *a++;
    if (*b && c + 1u < LCD_COLS) {
        c++;
        lcd_put(1, c, b, LCD_COLS);
    }
}

/* The edit pages (issue #31, the first of #8): the firmware's title (16 characters, 0xF940 on) above its parameters line, each value in the
 * template's three $ columns from a byte of the patch block. The title line's layout (the patch number at the right) is ours. */
enum { LCD_PAGE_PLAY, LCD_PAGE_OUTPUT, LCD_PAGE_CHASE, LCD_PAGE_MIDI, LCD_PAGES, LCD_PAGE_TUNE = LCD_PAGES };   /* (the tune page is SEL's, not EDIT's: #54) */
enum { LCD_SHOW_NUM, LCD_SHOW_NUM1, LCD_SHOW_CHASE, LCD_SHOW_ONOFF, LCD_SHOW_CONTROL };   /* the value; the value + 1; the chase mode's name;
                                                                                        * ON or OFF; MIDI-1's Control (#60: 0x7CB6, 6 each) */
typedef struct {
    uint8_t byte, lo, hi, col, show;                      /* the patch block's byte, its range, its column on the lower line, how shown */
} lcd_field_t;
typedef struct {
    const char *title, *line;
    uint8_t fields;
    lcd_field_t f[4];
    uint8_t own;                                          /* the values are not the patch's but the caller's (lcd_page_screen own[]) */
} lcd_page_t;
static const lcd_page_t lcd_pages[LCD_PAGES] = {
    {"", "", 0, {{0}}},
    {"Output Mode Edit", "Mode$$$  Rev$$$ Rbal$$$  Vol$$$", 4,           /* 0x6F9C */
     {{29, 0, 3, 4, LCD_SHOW_NUM1}, {30, 0, 31, 12, LCD_SHOW_NUM1}, {31, 0, 100, 20, LCD_SHOW_NUM}, {32, 0, 100, 28, LCD_SHOW_NUM}}},
    {"Chase Edit", "Mode$$$ Levl$$$ Time$$$", 3,                         /* 0x6FC4 */
     {{34, 0, 2, 4, LCD_SHOW_CHASE}, {35, 0, 100, 12, LCD_SHOW_NUM}, {36, 0, 100, 20, LCD_SHOW_NUM}}},
    /* the MIDI settings (issue #51), not the patch's: the basic channel (also the SysEx device ID), omni, and MIDI-1's Control (#60:
     * B.CH, G.CH, MdeOff). The title is the firmware's (0xF900's list); the line is ours, K1 to K3 left to right */
    {"MIDI Channel", "BasicCH$$$  Omni$$$  Control$$$$$$", 3,
     {{0, 0, 15, 7, LCD_SHOW_NUM1}, {1, 0, 1, 16, LCD_SHOW_ONOFF}, {2, 0, 2, 28, LCD_SHOW_CONTROL}}, 1},
};
static void lcd_page_screen(uint32_t page, char memory, uint32_t program, const uint8_t *p, const uint8_t *own)
{
    static const char chase[] = " ULULLULU";                                         /* 3 each, mode 0..2 (0x7B98) */
    const lcd_page_t *g = &lcd_pages[page];
    const uint8_t *pb = g->own ? own : p + 6u * 64u;
    char t[4];
    uint32_t i;
    lcd_clear();
    lcd_put(0, 0, g->title, 16);
    t[0] = memory;
    t[1] = '-';
    t[2] = (char)('1' + program / 8u % 8u);
    t[3] = (char)('1' + program % 8u);
    lcd_put(0, 36, t, 4);
    lcd_put(1, 0, g->line, LCD_COLS);
    for (i = 0; i < g->fields; i++) {
        const lcd_field_t *f = &g->f[i];
        uint32_t v = pb[f->byte] < f->lo ? f->lo : pb[f->byte] > f->hi ? f->hi : pb[f->byte];
        if (f->show == LCD_SHOW_CHASE) {
            lcd_put(1, f->col, chase + 3u * v, 3);
        } else if (f->show == LCD_SHOW_ONOFF) {
            lcd_put(1, f->col, v ? " ON" : "OFF", 3);
        } else if (f->show == LCD_SHOW_CONTROL) {
            static const char control[] = " B.CH  G.CH MdeOff";   /* (0x7CB6) */
            lcd_put(1, f->col, control + 6u * v, 6);
        } else {
            v += f->show == LCD_SHOW_NUM1 ? 1u : 0u;
            t[0] = v >= 100u ? (char)('0' + v / 100u) : ' ';
            t[1] = v >= 10u ? (char)('0' + v / 10u % 10u) : ' ';
            t[2] = (char)('0' + v % 10u);
            lcd_put(1, f->col, t, 3);
        }
    }
}

/* The TUNE/FUNCTION page (issue #54, SEL): the firmware's Master Tune and Key Transpose lines (0x7515 on), the tune 0..100 as 427.5 .. 452.6 Hz (0.251 Hz a step) */
static void lcd_tune_screen(uint32_t tune, int32_t transpose)
{
    char t[8];
    uint32_t hz = 4275u + tune * 251u / 100u, a = (uint32_t)(transpose < 0 ? -transpose : transpose);
    lcd_clear();
    lcd_put(0, 0, "Master Tune", LCD_COLS);
    t[0] = (char)('0' + hz / 1000u);
    t[1] = (char)('0' + hz / 100u % 10u);
    t[2] = (char)('0' + hz / 10u % 10u);
    t[3] = '.';
    t[4] = (char)('0' + hz % 10u);
    lcd_put(0, 16, t, 5);
    lcd_put(0, 22, "Hz", 2);
    lcd_put(1, 0, "Key Transpose =", LCD_COLS);
    t[0] = transpose < 0 ? '-' : transpose > 0 ? '+' : ' ';
    t[1] = a >= 10u ? (char)('0' + a / 10u) : ' ';
    t[2] = (char)('0' + a % 10u);
    lcd_put(1, 16, t, 3);
}

/* The LCD's height in pixels for a dot pitch (a dot's height in pixels): a margin of glass, the two lines of 8 dot rows each with a pitch
 * between them */
static uint32_t lcd_height(uint32_t pitch) { return 17u * pitch + 8u; }

/* The buffer drawn into the canvas from row y, across its 240 pixels */
static void lcd_draw(int32_t y, uint32_t pitch)
{
    uint32_t r, c, dy, dx;
    cv_rect(0, y, 240, (int32_t)lcd_height(pitch), LCD_GLASS);
    y += 4;
    for (r = 0; r < LCD_ROWS; r++, y += (int32_t)(9u * pitch))
        for (c = 0; c < LCD_COLS; c++) {
            uint8_t ch = (uint8_t)lcd_buf[r][c];
            const uint8_t *g = ch >= 0x20u && ch < 0x80u ? lcd_font[ch - 0x20u] : lcd_font[0];
            for (dy = 0; dy < 8u; dy++) {
                uint32_t bits = dy < 7u ? g[dy] : 0u;
                for (dx = 0; dx < 5u; dx++)
                    cv_rect((int32_t)(c * 6u + dx), y + (int32_t)(dy * pitch), 1, (int32_t)pitch,
                            bits >> (4u - dx) & 1u ? LCD_ON : LCD_OFF);
            }
        }
}

/* The folded view (issue #26): each 40-character line as two, columns 0..23 above 24..39, from the same buffer. Column 24 is where the play
 * screen's two halves part (both lines are blank in 23..26: the patch and its key mode / the tones) and where the firmware's 8-character
 * menu fields do. 10 pixels a character fill the 240 across: dots 2 x 2 pixels, the fifth 1 pixel wide, so a pixel parts the characters.
 * The lines are 16 pixels high, 4 apart. */
#define LCD_FOLD 24u
#define LCD_FOLDED_HEIGHT (4u * 16u + 3u * 4u + 8u)
static void lcd_draw_folded(int32_t y)
{
    uint32_t r, c, dy, dx;
    cv_rect(0, y, 240, (int32_t)LCD_FOLDED_HEIGHT, LCD_GLASS);
    y += 4;
    for (r = 0; r < 2u * LCD_ROWS; r++, y += 20)
        for (c = 0; c < LCD_FOLD; c++) {
            uint32_t at = (r % 2u) * LCD_FOLD + c;
            uint8_t ch = at < LCD_COLS ? (uint8_t)lcd_buf[r / 2u][at] : (uint8_t)' ';
            const uint8_t *g = ch >= 0x20u && ch < 0x80u ? lcd_font[ch - 0x20u] : lcd_font[0];
            for (dy = 0; dy < 8u; dy++) {
                uint32_t bits = dy < 7u ? g[dy] : 0u;
                for (dx = 0; dx < 5u; dx++)
                    cv_rect((int32_t)(c * 10u + dx * 2u), y + (int32_t)(dy * 2u), dx < 4u ? 2 : 1, 2,
                            bits >> (4u - dx) & 1u ? LCD_ON : LCD_OFF);
            }
        }
}
