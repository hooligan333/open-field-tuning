// The pack `tuning`: alternate tunings for OS 1.7.3+0. Card level 3, a frame and an input function, no table entries.
// It writes the firmware's note table A (128 words at 0x848c5270, analysis/tuning/FINDINGS.md), only after the CRC-32 of
// a run-time snapshot of it is the stock one, and before each later write only when the table still holds what it
// wrote last (else it stops writing). It holds no firmware byte: entries are built from the snapshot
// (core/tuning_core.h) and the tuning blocks of builtins.h or of .oft files in its folder `tuning`.
//
// Page: shift + metronome. Two columns: groups (blue knob) and the tunings of the group (second knob); third knob:
// root c..b; fourth: detune -50..+50 cents; knob presses: taken, ignored. A change is applied after 8 frames of rest.
// The picture (tuning.svg.make, analysis/screens/designs/tuning/final/DESIGN.md) shows the cursors as capsules behind
// the rows, the applied entry as a dot, and a monochord: bridge at root + cents, a cents arc, a dot per degree at its
// deviation. Its parts are found once and moved/shown only when a value changes. A part missing: logged once, the
// parts are hidden and the rows carry MARK (": ") as version 5 did.
// A file whose group and name equal a built-in's takes that built-in's row (the file plays); the built-in is not listed.
// Files: "<group>-<name>.oft" (first hyphen); no hyphen or an empty part: group "user". The choice is kept in the
// hidden folder `tuning-set` (tuning.set) and applied at the next start.
//
// Input function: key thread or audio thread; single volatile words, no log, only op1f_page_open/close.
// Frame function: everything else. No division, no 64-bit arithmetic, no floats. Compiled for the computer with
// OP1F_HOST and TUNING_TABLE (a buffer's address) by host/run.sh.

#include "build_m3_card.h"
#include "build_q_quiet.h"
#include "tuning_core.h"
#include "builtins.h"

typedef unsigned long addr_t;

#ifndef TUNING_TABLE
#define TUNING_TABLE 0x848c5270ul
#endif
#define TABLE ((volatile u32*)(addr_t)(TUNING_TABLE))

enum { KEY_METRONOME = 1, KNOB_GROUP = 0, KNOB_TUNING = 1, KNOB_ROOT = 2, KNOB_DETUNE = 3, KNOBS = 4,
       KEY_CLICK_FIRST = 54, KEY_CLICK_LAST = 57, KEY_PRESS_FIRST = 61, KEY_PRESS_LAST = 64 };
enum { ROWS = 7, FOLDER_FILES = 0, FOLDER_SET = 1, FILES_MOST = 48, GROUPS_MOST = 12, NAMES_ROOM = 64 * OP1F_FILE_NAME,
       ENTRIES_MOST = 1 + TC_BUILTIN_COUNT + FILES_MOST, LINE_ROOM = 64, REST_FRAMES = 8, SAVE_AFTER_MS = 2000,
       APPLIED_LINES = 50, PERIOD_FRAMES = 256, NOT_NOW_FRAMES = 64, LOOK_FRAMES = 32, BAD_SHOW_MS = 3000 };
// text widths in pixels, matching tuning.svg.make (version 7): title x 34 (ends 12 px before the ROOT word at 298),
// root value x 340 (ends before the CENTS word at 378), cents value x 430, groups x 24 (their capsule to 118, divider
// 124), tunings x 142 (their capsule to 460); screen 480. The unit cuts a longer text by dropping its
// last letters, with no mark: so every text is held to its room here before it is set (fit, below), and the width
// handed to the base is only the last guard.
enum { TITLE_WIDTH = 252, ROOT_WIDTH = 28, CENTS_WIDTH = 48, GROUP_WIDTH = 92, TUNING_WIDTH = 310 };
// fallback (a part missing): a group row also carries ': ', so it may run to the divider at 124 (x 24 + 98 = 122)
enum { FALLBACK_GROUPS_PX = 98 };
enum { FRAME_LEVEL_3 = 36, INPUT_LEVEL_2 = 36 };
enum { KIND_OFF = 0, KIND_BUILTIN = 1, KIND_FILE = 2 };
enum { SHOWS_ACTIVE = 0, SHOWS_BAD = 1, SHOWS_NOT_STOCK = 2 };
// tuning.set: "OTS1", kind, root, detune (signed byte), 0, name[40] zero padded, CRC-32 of bytes 0..47 (LE)
enum { SET_BYTES = 52, SET_NAME = 8, SET_SUM = 48 };

static const u32 STOCK_SUM = 0x492003fcu;                 // zlib.crc32 of the 512 stock bytes (record 235 of OS 1.7.3)
static const char LOG_FILE[] = "op1f/tuning.cpp";
static const char SET_FILE[] = "tuning.set";
static const char EQUAL[] = "equal", USER[] = "user", OFF_NAME[] = "12 equal";
static const char MARK[] = ": ", NO_MARK[] = "  ";          // fallback only (a part missing): the cursor row's mark
// The picture's parts (DESIGN.md "Parts"); the first five start hidden.
enum { P_WARN, P_BRIDGEO, P_DOTO, P_SLACK, P_LOCK, P_ICON, P_GCUR, P_TCUR, P_GACT, P_TACT, P_BRIDGE, P_ARC, P_DOT, P_DEG0,
       PARTS = P_DEG0 + 12, START_HIDDEN = 5, TEXTS = 3 + 2 * ROWS, ROW_STEP = 17 * 1024, BRIDGE_X = 430, BRIDGE_Y = 188 };
static const char* const PART_NAMES[PARTS] = { "tuning_warn", "tuning_bridgeo", "tuning_doto", "tuning_slack", "tuning_lock",
    "tuning_icon", "tuning_gcur", "tuning_tcur", "tuning_gact", "tuning_tact", "tuning_bridge", "tuning_arc", "tuning_dot",
    "tuning_deg0", "tuning_deg1", "tuning_deg2", "tuning_deg3", "tuning_deg4", "tuning_deg5", "tuning_deg6", "tuning_deg7",
    "tuning_deg8", "tuning_deg9", "tuning_deg10", "tuning_deg11" };
// the bridge's x (1024ths of a pixel, from x 430 = root c) at 12-equal stations k = -1..12: 30 + 400 * 2^(-k/12) - 430
static const int BXK[14] = { 24356, 0, -22989, -44688, -65169, -84500, -102747, -119969, -136225, -151568, -166050, -179720, -192622, -204800 };
// sin and cos of 0.9 * c degrees, c = 0..50 cents, in 1024ths: the dot turns 45 degrees at 50 cents
static const short SIN[51] = { 0,16,32,48,64,80,96,112,128,144,160,176,192,208,223,239,255,270,286,301,316,332,347,362,377,392,407,421,436,450,465,479,493,507,521,535,549,562,576,589,602,615,628,640,653,665,677,689,701,713,724 };
static const short COS[51] = { 1024,1024,1023,1023,1022,1021,1019,1018,1016,1014,1011,1009,1006,1003,999,996,992,988,983,979,974,969,963,958,952,946,940,933,927,920,912,905,897,889,881,873,865,856,847,838,828,819,809,799,789,779,768,757,746,735,724 };
// 1024ths of a pixel a cent at degree k's station (1024 * 400 * 2^(-k/12) * ln 2 / 1200)
static const short S_K[12] = { 237, 223, 211, 199, 188, 177, 167, 158, 149, 141, 133, 125 };
static const char* const GROUP_PARTS[ROWS] = { "tuning_group1", "tuning_group2", "tuning_group3", "tuning_group4", "tuning_group5", "tuning_group6", "tuning_group7" };
static const char* const ROW_PARTS[ROWS] = { "tuning_row1", "tuning_row2", "tuning_row3", "tuning_row4", "tuning_row5", "tuning_row6", "tuning_row7" };
static const char* const ROOTS[12] = { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };

extern "C" const Op1fCard op1f_card_tuning OP1F_NAMED("op1f_card_tuning");

// Written by the input function only, read by the frame function.
static volatile int group_steps, tuning_steps, root_steps, detune_steps;   // totals of the knobs' steps since the start
static volatile u32 keys, turns;
static void* volatile document;

// The frame function's own (zeroed data).
static u32 snapshot[TC_NOTES];                             // the table as found at the start
static u32 staging[TC_NOTES];
static u8 file_buf[TC_FILE];
static u8 set_buf[SET_BYTES];
static char names[NAMES_ROOM];

// Entries are numbered as before: 0 off ("12 equal"), 1..TC_BUILTIN_COUNT built-ins, then the files.
static struct {
    u32 started, stock, sum_found, table_sum, outside;      // table_sum: CRC-32 of what we last wrote (or found)
    int files;                                             // .oft files listed
    const char* file[FILES_MOST];                          // each one's name in `names`
    const char* file_shown[FILES_MOST];                    // its name in its group (no ".oft")
    int file_shown_len[FILES_MOST], file_group[FILES_MOST];
    int file_replaces[FILES_MOST];                         // 1 + the built-in this file stands in for, 0: none
    int builtin_file[TC_BUILTIN_COUNT];                    // 1 + the file that stands in for this built-in, 0: none
    int groups;
    const char* group_name[GROUPS_MOST];
    int group_len[GROUPS_MOST], group_first[GROUPS_MOST], group_count[GROUPS_MOST], group_chosen[GROUPS_MOST];
    int kept, kept_kind[GROUPS_MOST];                      // each group's chosen tuning over a new listing
    char kept_name[GROUPS_MOST][OP1F_FILE_NAME];
    int entry[ENTRIES_MOST];                               // entry numbers, group by group
    int listed, looks;                                     // what op1f_list answered last
    int group, item, group_top, item_top, root, detune;
    int seen_group, seen_tuning, seen_root, seen_detune;
    u32 pending, rest;
    int active_kind, active_root, active_detune;
    char active_name[OP1F_FILE_NAME];                      // a file's name with .oft, or a built-in's name
    char active_group[OP1F_FILE_NAME], active_shown[OP1F_FILE_NAME];
    u32 shows;
    char bad_name[OP1F_FILE_NAME];
    u32 bad_until;                                         // 'x: not valid' shows until then, then the active title
    u32 changed_ms, written_sum, written_any, failed_sum, failed_any;
    u32 applied, refused, saves, not_now, save_failed, texts_missed, save_wait;
    u32 disk, shown;
    char line[LINE_ROOM];
    int countdown;
    QuietGate quiet;
    int active_g, active_item;                             // the applied entry's group and row (-1: not listed)
    int dev[12];                                           // degrees c..b of the applied tuning, cents from 12 equal (-50..+50)
    u32 parts_ok, parts_logged;                            // every part found; the missing ones said once
    u32 part_on[PARTS];                                    // what each part was last shown as (the picture: on)
    int part_m[PARTS][9];                                  // each part's last matrix (the picture: none = identity)
    u32 text_set;                                          // bit p: texts[p] holds what part p was last asked to show
    char texts[TEXTS][LINE_ROOM];
} st;
static void* part[PARTS];                                   // the picture's parts, found once (0: missing)

extern "C" int tuning_input(const Op1fInput* e) OP1F_NAMED("tuning_input");
extern "C" int tuning_input(const Op1fInput* e) {
    if (e->bytes < (u32)INPUT_LEVEL_2) return 0;
    if (e->what == (u32)OP1F_DOWN_SHIFT && e->key == (u32)KEY_METRONOME) {     // shift + metronome, any mode
        if (document == 0) return 0;
        ++keys;
        if (e->mine) op1f_page_close(&op1f_card_tuning);
        else op1f_page_open(&op1f_card_tuning, document);
        return 1;
    }
    if (e->what == (u32)OP1F_UP) return 1;                                     // the up of a down we took
    if (!e->mine) return 0;
    if (e->what == (u32)OP1F_TURN && e->key < (u32)KNOBS) {               // all four knobs while the page shows
        const int step = e->turn >> 10;                                        // 1024 a step
        if (e->key == (u32)KNOB_GROUP) group_steps = group_steps + step;
        else if (e->key == (u32)KNOB_TUNING) tuning_steps = tuning_steps + step;
        else if (e->key == (u32)KNOB_DETUNE) detune_steps = detune_steps + step;
        else root_steps = root_steps + step;
        ++turns;
        return 1;
    }
    if ((e->what == (u32)OP1F_DOWN || e->what == (u32)OP1F_DOWN_SHIFT) &&
        ((e->key >= (u32)KEY_CLICK_FIRST && e->key <= (u32)KEY_CLICK_LAST) || (e->key >= (u32)KEY_PRESS_FIRST && e->key <= (u32)KEY_PRESS_LAST)))
        return 1;                                                               // knob clicks and presses: nothing hidden moves
    return 0;
}

// ---- small helpers (no library)
static u32 put(char* out, u32 at, u32 room, const char* text) {
    while (*text && at + 1 < room) out[at++] = *text++;
    out[at] = 0;
    return at;
}
static u32 put_shown(char* out, u32 at, u32 room, const char* text, int count) {   // small letters, proven signs only
    for (int i = 0; i < count && text[i] && at + 1 < room; ++i) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == ':' || c == ',' || c == '.' ||
                   c == '/' || c == '#' || c == '+' || c == '-')) c = '-';    // _ ( ) ; > ... may draw '?' or nothing
        out[at++] = c;
    }
    out[at] = 0;
    return at;
}
static u32 put_signed(char* out, u32 at, u32 room, int d) {              // "+12", "-5", "0" (|d| < 100)
    if (d) { char s[2] = { d < 0 ? '-' : '+', 0 }; at = put(out, at, room, s); }
    u32 n = (u32)(d < 0 ? -d : d), tens = 0;
    while (n >= 10u) { n -= 10u; ++tens; }
    char digits[3] = { (char)('0' + tens), (char)('0' + n), 0 };
    return put(out, at, room, tens ? digits : digits + 1);
}
static u32 put_cents(char* out, u32 at, u32 room, int d) {               // "+12 cents", "-5 cents", "0 cents" (log lines)
    return put(out, put_signed(out, at, room, d), room, " cents");
}
static int length(const char* s) { int n = 0; while (s[n]) ++n; return n; }

// ---- how wide the unit draws a text (version 7)
// The unit's letters are as tall as their placeholder (version 6's were 15 px tall: about 14 px a letter, where the
// design had counted 10), and a text wider than the width it is set with loses its last letters with no mark:
// version 6 showed `gamel` for the group and `gamelan / slend` for the title. So the page's letters are 12 px tall
// in the lists and 13 px in the top row, and a text is held to its room here, before it is set. LETTERS has, for
// those two heights, each character's width in pixels and the largest kerning of any pair it begins (`after`) or
// ends (`before`): a pair's kerning is at most the smaller of the first's `after` and the second's `before`, and the
// unit puts LETTER_GAP pixels between two letters. px_most is therefore never under the width the unit lays out
// (analysis/screens/tools/unit_text.py wrote the numbers from the unit's layout, `tables 12 13`; host/fit_expect.py
// holds px_most and fit to that layout, text by text). Only put_shown's characters reach a text.
struct Letter { unsigned char w, after, before; };
enum { LIST_LETTERS = 0, TOP_LETTERS = 1, LETTER_KINDS = 44, LETTER_GAP = 3, FIT_SPARE = 2 };
static const char LETTER_SIGNS[] = " :,./#+-";                              // behind a..z and 0..9
static const Letter LETTERS[2][LETTER_KINDS] = {
    {   // 12 px tall
        {11,1,1}, {9,1,1}, {9,1,1}, {9,1,1}, {8,2,2}, {8,1,1}, {9,2,1}, {8,1,1}, {0,2,2}, {9,1,0}, {9,1,1}, {7,1,2}, {10,1,1},
        {8,1,2}, {9,1,1}, {9,1,1}, {9,0,1}, {9,1,1}, {9,1,1}, {9,1,1}, {9,1,1}, {9,1,1}, {11,0,1}, {9,1,1}, {8,1,0}, {9,0,0},
        {10,1,0}, {0,3,3}, {10,1,1}, {10,1,0}, {10,0,0}, {10,0,1}, {10,0,0}, {10,0,0}, {10,0,0}, {10,0,0},
        {7,0,0}, {3,0,1}, {3,0,0}, {3,0,0}, {6,0,0}, {9,0,0}, {9,2,0}, {9,1,0} },
    {   // 13 px tall
        {12,1,1}, {10,1,1}, {10,1,1}, {10,1,2}, {9,2,2}, {9,1,2}, {10,2,1}, {9,1,1}, {0,3,2}, {10,1,0}, {10,1,1}, {8,1,3}, {10,2,1},
        {9,2,2}, {10,1,1}, {10,1,2}, {10,0,1}, {10,2,1}, {10,1,1}, {10,1,1}, {10,1,1}, {10,1,1}, {12,0,1}, {10,2,1}, {9,1,0}, {10,0,0},
        {10,1,0}, {0,3,3}, {10,1,1}, {10,1,0}, {11,0,0}, {10,0,1}, {10,0,0}, {10,0,0}, {10,0,0}, {10,0,0},
        {7,0,0}, {3,0,2}, {3,0,0}, {3,0,0}, {6,0,0}, {10,0,0}, {10,2,0}, {10,1,0} } };
static int letter_of(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '0' && c <= '9') return 26 + (c - '0');
    for (int i = 0; LETTER_SIGNS[i]; ++i) if (c == LETTER_SIGNS[i]) return 36 + i;
    return 'w' - 'a';                                                        // none comes (put_shown): the widest
}
static int px_most(const char* t, int kind) {                               // at least the text's width on the unit, pixels
    const Letter* l = LETTERS[kind];
    int px = 0, last = -1;
    for (; *t; ++t) {
        const int k = letter_of(*t);
        if (last >= 0) px += (int)LETTER_GAP + (int)(l[last].after < l[k].before ? l[last].after : l[k].before);
        px += (int)l[k].w;
        last = k;
    }
    return px;
}
static int fits(const char* t, int kind, int room) { return px_most(t, kind) + (int)FIT_SPARE <= room; }
// A text of at most LINE_ROOM - 1 characters held to `room` pixels: whole when it fits, else its longest beginning
// that fits with `..` behind it (a space before the mark goes). Nothing fits: empty.
static void fit(char* t, int kind, int room) {
    if (fits(t, kind, room)) return;
    int n = length(t);
    if (n > (int)LINE_ROOM - 2) n = (int)LINE_ROOM - 2;                     // the mark's room in the line
    while (n > 0) {
        --n;
        while (n > 0 && t[n - 1] == ' ') --n;
        t[n] = '.'; t[n + 1] = '.'; t[n + 2] = 0;
        if (fits(t, kind, room)) return;
    }
    t[0] = 0;
}
static int same(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void copy_name(char* to, const char* from) {
    int i = 0;
    for (; i < OP1F_FILE_NAME - 1 && from[i]; ++i) to[i] = from[i];
    for (; i < OP1F_FILE_NAME; ++i) to[i] = 0;
}
static void copy_part(char* to, const char* from, int n) {                // n bytes of from, then zeros
    int i = 0;
    for (; i < OP1F_FILE_NAME - 1 && i < n; ++i) to[i] = from[i];
    for (; i < OP1F_FILE_NAME; ++i) to[i] = 0;
}
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
static int same_lower(const char* a, int an, const char* b, int bn) {
    if (an != bn) return 0;
    for (int i = 0; i < an; ++i) if (lower(a[i]) != lower(b[i])) return 0;
    return 1;
}
static int before(const char* a, int an, const char* b, int bn) {          // a sorts before b (lower-cased bytes)
    for (int i = 0; i < an && i < bn; ++i) {
        const unsigned char x = (unsigned char)lower(a[i]), y = (unsigned char)lower(b[i]);
        if (x != y) return x < y;
    }
    return an < bn;
}
static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---- entries
static int kind_of(int i) { return i == 0 ? KIND_OFF : i <= TC_BUILTIN_COUNT ? KIND_BUILTIN : KIND_FILE; }
static const char* name_of(int i) {                                     // the settings' name: a file's whole name
    return i == 0 ? "" : i <= TC_BUILTIN_COUNT ? TC_BUILTINS[i - 1].name : st.file[i - 1 - TC_BUILTIN_COUNT];
}
static const char* shown_of(int i, int* n) {                            // the name in its group's column
    const char* s = i == 0 ? OFF_NAME : i <= TC_BUILTIN_COUNT ? TC_BUILTINS[i - 1].name : 0;
    if (s) { *n = length(s); return s; }
    *n = st.file_shown_len[i - 1 - TC_BUILTIN_COUNT];
    return st.file_shown[i - 1 - TC_BUILTIN_COUNT];
}
static int find(int kind, const char* name) {
    if (kind == KIND_OFF) return 0;
    if (kind == KIND_BUILTIN) {
        for (int k = 0; k < TC_BUILTIN_COUNT; ++k)
            if (same(TC_BUILTINS[k].name, name))                    // a file in its row stands in for it
                return st.builtin_file[k] ? 1 + TC_BUILTIN_COUNT + st.builtin_file[k] - 1 : 1 + k;
    } else if (kind == KIND_FILE) {
        for (int k = 0; k < st.files; ++k)
            if (same(st.file[k], name)) return 1 + TC_BUILTIN_COUNT + k;
    }
    return -1;
}
static int current() { return st.entry[st.group_first[st.group] + st.item]; }
static int where(int i, int* item) {                                    // the group of entry i (-1: not listed)
    for (int g = 0; g < st.groups; ++g)
        for (int k = 0; k < st.group_count[g]; ++k)
            if (st.entry[st.group_first[g] + k] == i) { *item = k; return g; }
    return -1;
}
static void place(int i) {                                              // both cursors on entry i
    int item = 0;
    const int g = where(i, &item);
    if (g < 0) return;
    st.group = g;
    st.item = item;
    st.group_chosen[g] = item;
}
static void keep_in() {                                                 // cursors inside the lists
    st.group = clamp(st.group, 0, st.groups - 1);
    st.item = clamp(st.item, 0, st.group_count[st.group] - 1);
}

static int ends_oft(const char* s, int n) {
    return n > 4 && s[n - 4] == '.' && (s[n - 3] | 32) == 'o' && (s[n - 2] | 32) == 'f' && (s[n - 1] | 32) == 't';
}
// A file's group and name by the first hyphen; 0 when it has none or a part is empty.
static int split(const char* s, int* group_len, const char** shown, int* shown_len) {
    const int n = length(s) - 4;
    int h = 0;
    while (h < n && s[h] != '-') ++h;
    if (h == 0 || h >= n - 1) return 0;
    *group_len = h;
    *shown = s + h + 1;
    *shown_len = n - h - 1;
    return 1;
}
static int group_find(const char* s, int n) {
    for (int g = 0; g < st.groups; ++g)
        if (same_lower(st.group_name[g], st.group_len[g], s, n)) return g;
    return -1;
}
// 1 + the built-in whose group and name a file name carries (letter case ignored), 0: none.
static int builtin_named(const char* s) {
    int gl, sl;
    const char* sp;
    if (!ends_oft(s, length(s)) || !split(s, &gl, &sp, &sl)) return 0;
    for (int k = 0; k < TC_BUILTIN_COUNT; ++k) {
        const char* bg = TC_BUILTINS[k].group;
        const char* bn = TC_BUILTINS[k].name;
        if (same_lower(bg, length(bg), s, gl) && same_lower(bn, length(bn), sp, sl)) return 1 + k;
    }
    return 0;
}
// find(), and a file that is gone but named as a built-in resolves to that built-in's row.
static int find_or_builtin(int kind, const char* name) {
    const int i = find(kind, name);
    const int b = i < 0 && kind == KIND_FILE ? builtin_named(name) : 0;
    return b ? find(KIND_BUILTIN, TC_BUILTINS[b - 1].name) : i;
}
static void add_group(const char* s, int n) {
    st.group_name[st.groups] = s;
    st.group_len[st.groups] = n;
    ++st.groups;
}

// The groups and their entries from the built-ins and the listed files; every cursor memory back to the first.
static void make_groups() {
    st.groups = 0;
    add_group(EQUAL, 5);
    for (int k = 0; k < TC_BUILTIN_COUNT; ++k) {
        const char* g = TC_BUILTINS[k].group;
        if (group_find(g, length(g)) < 0 && st.groups < (int)GROUPS_MOST - 1) add_group(g, length(g));
    }
    const int fixed = st.groups;
    int gl, sl;
    const char* sp;
    for (int f = 0; f < st.files; ++f)                                 // new groups, in the disk's order, while room
        if (split(st.file[f], &gl, &sp, &sl) && group_find(st.file[f], gl) < 0 && !same_lower(st.file[f], gl, USER, 4) &&
            st.groups < (int)GROUPS_MOST - 1)
            add_group(st.file[f], gl);
    for (int a = fixed + 1; a < st.groups; ++a) {                      // new groups in alphabetical order
        const char* s = st.group_name[a];
        const int n = st.group_len[a];
        int b = a;
        for (; b > fixed && before(s, n, st.group_name[b - 1], st.group_len[b - 1]); --b) {
            st.group_name[b] = st.group_name[b - 1];
            st.group_len[b] = st.group_len[b - 1];
        }
        st.group_name[b] = s;
        st.group_len[b] = n;
    }
    const int user = st.groups;
    int users = 0;
    for (int f = 0; f < st.files; ++f) {
        int g = -1;
        if (split(st.file[f], &gl, &sp, &sl)) {
            g = same_lower(st.file[f], gl, USER, 4) ? user : group_find(st.file[f], gl);
            st.file_shown[f] = sp;
            st.file_shown_len[f] = sl;
        }
        if (g < 0) {                                                   // no group, or no room for its group
            g = user;
            st.file_shown[f] = st.file[f];
            st.file_shown_len[f] = length(st.file[f]) - 4;
        }
        st.file_group[f] = g;
        users += g == user;
    }
    if (users) add_group(USER, 4);
    for (int k = 0; k < TC_BUILTIN_COUNT; ++k) st.builtin_file[k] = 0;
    for (int f = 0; f < st.files; ++f) {                               // a file named as a built-in of its group takes its row
        st.file_replaces[f] = 0;
        const int g = st.file_group[f];
        if (g >= user) continue;
        for (int k = 0; k < TC_BUILTIN_COUNT; ++k) {
            const char* bg = TC_BUILTINS[k].group;
            const char* bn = TC_BUILTINS[k].name;
            if (!st.builtin_file[k] && same_lower(bg, length(bg), st.group_name[g], st.group_len[g]) &&
                same_lower(bn, length(bn), st.file_shown[f], st.file_shown_len[f])) {
                st.builtin_file[k] = 1 + f;
                st.file_replaces[f] = 1 + k;
                break;
            }
        }
    }
    int n = 0;
    for (int g = 0; g < st.groups; ++g) {
        st.group_first[g] = n;
        st.group_chosen[g] = 0;
        if (g == 0) st.entry[n++] = 0;
        for (int k = 0; k < TC_BUILTIN_COUNT; ++k)
            if (g < user && same_lower(TC_BUILTINS[k].group, length(TC_BUILTINS[k].group), st.group_name[g], st.group_len[g]))
                st.entry[n++] = st.builtin_file[k] ? 1 + TC_BUILTIN_COUNT + st.builtin_file[k] - 1 : 1 + k;
        const int from = n;
        for (int f = 0; f < st.files; ++f) {                           // the group's files, by insertion
            if (st.file_group[f] != g || st.file_replaces[f]) continue;
            int b = n++;
            for (; b > from && before(st.file_shown[f], st.file_shown_len[f], st.file_shown[st.entry[b - 1] - 1 - TC_BUILTIN_COUNT],
                                      st.file_shown_len[st.entry[b - 1] - 1 - TC_BUILTIN_COUNT]); --b)
                st.entry[b] = st.entry[b - 1];
            st.entry[b] = 1 + TC_BUILTIN_COUNT + f;
        }
        st.group_count[g] = n - st.group_first[g];
    }
}

static void look() {
    ++st.looks;
    int got = op1f_list(&op1f_card_tuning, FOLDER_FILES, names, NAMES_ROOM);
    st.listed = got;
    st.files = 0;
    u32 at = 0;
    for (int i = 0; i < got && at < (u32)NAMES_ROOM; ++i) {
        const char* s = names + at;
        int n = 0;
        while (at + (u32)n < (u32)NAMES_ROOM && s[n]) ++n;
        if (at + (u32)n >= (u32)NAMES_ROOM) break;
        if (ends_oft(s, n) && st.files < (int)FILES_MOST) st.file[st.files++] = s;
        at += (u32)n + 1u;
    }
    make_groups();
}

// Each group's tuning cursor by kind and name before a new listing, and found again after it.
static void keep_chosen() {
    st.kept = st.groups;
    for (int g = 0; g < st.groups; ++g) {
        const int i = st.entry[st.group_first[g] + st.group_chosen[g]];
        st.kept_kind[g] = kind_of(i);
        copy_name(st.kept_name[g], name_of(i));
    }
}
static void find_chosen() {
    for (int k = 0; k < st.kept; ++k) {
        int item = 0;
        const int i = find(st.kept_kind[k], st.kept_name[k]);
        const int g = i < 0 ? -1 : where(i, &item);
        if (g >= 0) st.group_chosen[g] = item;
    }
}

// ---- applying
static void say_applied(int ok) {
    if (st.applied + st.refused > (u32)APPLIED_LINES) return;
    char d[16];
    put_cents(d, 0, sizeof d, st.detune);
    if (ok)
        op1f_log(3, LOG_FILE, 2, "tuning: applied '%s', root %s, %s (%u applied)", st.active_kind == KIND_OFF ? "off" : st.active_name,
                 ROOTS[st.root], d, st.applied);
    else
        op1f_log(2, LOG_FILE, 3, "tuning: '%s' not valid, nothing changed (%u refused)", st.bad_name, st.refused);
}

// 1 when the table still holds what we wrote last (or found at the start); else writing stops for good.
static int table_ours() {
    for (int n = 0; n < TC_NOTES; ++n) staging[n] = TABLE[n];
    const u32 sum = tc_crc32((const u8*)staging, 4u * TC_NOTES);
    if (sum == st.table_sum) return 1;
    st.stock = 0;
    st.outside = 1;
    st.shows = SHOWS_NOT_STOCK;
    op1f_log(2, LOG_FILE, 6, "tuning: note table changed from outside (sum %08x, ours %08x): nothing more will be written", sum, st.table_sum);
    return 0;
}

// The cents (-50..+50, clamped) of entry e against the stock entry s: the j whose s * TC_CENTS[j] / 2^30 is nearest e,
// less 50. Binary search for the last j at or below e, then the nearer of j and j + 1; no division, no 64-bit arithmetic.
static int deviation(u32 s, u32 e) {
    u32 v = 0;
    if (!tc_mul_q30(s, TC_CENTS[0], &v) || e <= v) return -TC_DETUNE_MOST;
    int lo = 0, hi = 2 * TC_DETUNE_MOST;                                // TC_CENTS[lo] at or below e
    while (lo < hi) {
        const int mid = (lo + hi + 1) >> 1;
        if (tc_mul_q30(s, TC_CENTS[mid], &v) && v <= e) lo = mid; else hi = mid - 1;
    }
    if (lo < 2 * TC_DETUNE_MOST) {
        u32 below = 0, above = 0;
        tc_mul_q30(s, TC_CENTS[lo], &below);
        if (tc_mul_q30(s, TC_CENTS[lo + 1], &above) && above - e < e - below) ++lo;
    }
    return lo - TC_DETUNE_MOST;
}

// Entry i with the knobs' root and detune into the table, or nothing at all.
static int apply(int i, u32 ms) {
    if (!st.stock || !table_ours()) return 0;
    const int kind = kind_of(i);
    const char* name = name_of(i);
    int ok = 1;
    if (kind == KIND_OFF) {
        for (int n = 0; n < TC_NOTES; ++n) staging[n] = snapshot[n];
    } else {
        const u8* block = 0;
        if (kind == KIND_BUILTIN) block = TC_BUILTINS[i - 1].block;
        else if (op1f_read(&op1f_card_tuning, FOLDER_FILES, name, file_buf, TC_FILE) == (int)TC_FILE && tc_file_ok(file_buf, TC_FILE))
            block = file_buf + 4;
        ok = block != 0 && tc_build(snapshot, block, st.root, staging);
    }
    int nd[12];                                                         // the degree dots: keys 60..71 before the detune
    for (int k = 0; k < 12; ++k) nd[k] = ok && kind != KIND_OFF ? deviation(snapshot[60 + k], staging[60 + k]) : 0;
    ok = ok && tc_detune(staging, TC_CENTS[st.detune + TC_DETUNE_MOST]);
    if (!ok) {
        ++st.refused;
        st.shows = SHOWS_BAD;
        st.bad_until = ms + BAD_SHOW_MS;
        copy_name(st.bad_name, name);
        say_applied(0);
        return 0;
    }
    for (int n = 0; n < TC_NOTES; ++n) TABLE[n] = staging[n];
    st.table_sum = tc_crc32((const u8*)staging, 4u * TC_NOTES);
    ++st.applied;
    st.active_kind = kind;
    st.active_root = st.root;
    st.active_detune = st.detune;
    copy_name(st.active_name, name);
    int item = 0, n = 0;
    const int g = where(i, &item);
    copy_part(st.active_group, g < 0 ? "" : st.group_name[g], g < 0 ? 0 : st.group_len[g]);
    const char* s = shown_of(i, &n);
    copy_part(st.active_shown, s, n);
    st.active_g = g;
    st.active_item = g < 0 ? 0 : item;
    for (int k = 0; k < 12; ++k) st.dev[k] = nd[k];                     // a refused tuning (above) leaves the dots as they are
    st.shows = SHOWS_ACTIVE;
    st.changed_ms = ms;
    say_applied(1);
    return 1;
}

// ---- remembering
static void make_set() {
    int kind = st.active_kind;
    const char* name = st.active_name;
    if (kind == KIND_FILE) {                                            // a file in a built-in's row is saved as that built-in:
        const int i = find(KIND_FILE, name);                            // find() maps it back to the file while the file is there,
        const int f = i - 1 - TC_BUILTIN_COUNT;                         // and the built-in returns if the file is deleted
        if (i > 0 && st.file_replaces[f]) { kind = KIND_BUILTIN; name = TC_BUILTINS[st.file_replaces[f] - 1].name; }
    }
    set_buf[0] = 'O'; set_buf[1] = 'T'; set_buf[2] = 'S'; set_buf[3] = '1';
    set_buf[4] = (u8)kind;
    set_buf[5] = (u8)st.active_root;
    set_buf[6] = (u8)st.active_detune;
    set_buf[7] = 0;
    for (int k = 0; k < OP1F_FILE_NAME; ++k) set_buf[SET_NAME + k] = 0;
    for (int k = 0; k < OP1F_FILE_NAME && name[k]; ++k) set_buf[SET_NAME + k] = (u8)name[k];
    const u32 sum = tc_crc32(set_buf, SET_SUM);
    for (int k = 0; k < 4; ++k) set_buf[SET_SUM + k] = (u8)(sum >> (8 * k));
}

// The remembered choice: its entry (or -1), with root and detune set; -2 when there is no valid file.
static int read_set() {
    int got = op1f_read(&op1f_card_tuning, FOLDER_SET, SET_FILE, set_buf, SET_BYTES);
    if (got != (int)SET_BYTES || set_buf[0] != 'O' || set_buf[1] != 'T' || set_buf[2] != 'S' || set_buf[3] != '1') return -2;
    const u32 sum = tc_crc32(set_buf, SET_SUM);
    if (sum != tc_get32(set_buf + SET_SUM)) return -2;
    const int kind = set_buf[4], root = set_buf[5], detune = (int)(signed char)set_buf[6];
    if (kind > KIND_FILE || root > 11 || detune < -TC_DETUNE_MOST || detune > TC_DETUNE_MOST || set_buf[SET_SUM - 1] != 0) return -2;
    st.written_sum = sum;
    st.written_any = 1;
    st.root = root;
    st.detune = detune;
    copy_name(st.bad_name, (const char*)set_buf + SET_NAME);               // what was remembered, for the start line
    return find_or_builtin(kind, (const char*)set_buf + SET_NAME);        // a stand-in saved as a file, now gone: its built-in
}

static void save(u32 ms, u32 disk) {
    if (!st.stock || disk || ms - st.changed_ms < (u32)SAVE_AFTER_MS) return;
    if (st.save_wait) { --st.save_wait; return; }
    make_set();
    const u32 sum = tc_get32(set_buf + SET_SUM);
    if ((st.written_any && sum == st.written_sum) || (st.failed_any && sum == st.failed_sum)) return;
    int answer = op1f_write(&op1f_card_tuning, FOLDER_SET, SET_FILE, set_buf, SET_BYTES);
    if (answer == (int)SET_BYTES) {
        ++st.saves;
        st.written_sum = sum;
        st.written_any = 1;
    } else if (answer == OP1F_NOT_NOW) {
        ++st.not_now;
        st.save_wait = NOT_NOW_FRAMES;                                      // not every frame
    } else {
        ++st.save_failed;
        st.failed_sum = sum;
        st.failed_any = 1;
    }
}

// ---- the page
static u32 put_active(char* t, u32 at) {                                // "maqam / rast", "equal / 12 equal"
    at = put_shown(t, at, LINE_ROOM, st.active_kind == KIND_OFF ? EQUAL : st.active_group, OP1F_FILE_NAME);
    at = put(t, at, LINE_ROOM, " / ");
    return put_shown(t, at, LINE_ROOM, st.active_kind == KIND_OFF ? OFF_NAME : st.active_shown, OP1F_FILE_NAME);
}
// The title for what plays: group and name while that fits the title's room (every built-in's does); a longer one
// is the name alone (its group has the blue dot in the list), and a name too long for that ends in `..`.
static void put_title(char* t) {
    put_active(t, 0);
    if (fits(t, TOP_LETTERS, TITLE_WIDTH)) return;
    put_shown(t, 0, LINE_ROOM, st.active_kind == KIND_OFF ? OFF_NAME : st.active_shown, OP1F_FILE_NAME);
    fit(t, TOP_LETTERS, TITLE_WIDTH);
}
// 'name: not valid' for a refused file: the name cut here (with `..`) until the whole fits, so ': not valid' is never cut
static void put_refused(char* t) {
    int whole = length(st.bad_name);
    if (ends_oft(st.bad_name, whole)) whole -= 4;
    for (int n = whole; ; --n) {
        int shown = n;
        while (shown < whole && shown > 0 && st.bad_name[shown - 1] == ' ') --shown;   // no space before the mark
        u32 at = put_shown(t, 0, LINE_ROOM, st.bad_name, shown);
        if (shown < whole) at = put(t, at, LINE_ROOM, "..");
        put(t, at, LINE_ROOM, ": not valid");
        if (n <= 0 || fits(t, TOP_LETTERS, TITLE_WIDTH)) return;
    }
}

static int top(int first, int cursor, int count) {                       // a window of ROWS rows that holds the cursor
    if (cursor < first) first = cursor;
    if (cursor >= first + (int)ROWS) first = cursor - (int)ROWS + 1;
    return clamp(first, 0, count > (int)ROWS ? count - (int)ROWS : 0);
}

// Text p (0 title, 1 root, 2 cents, 3.. groups, 3 + ROWS.. tunings) set only when it differs from what was last asked.
// A text the base refuses is counted once and asked again only when the wanted text changes (not every frame).
static void text(int p, const char* name, const char* t, int width) {
    char* was = st.texts[p];
    if ((st.text_set >> p) & 1u) { int i = 0; while (was[i] && was[i] == t[i]) ++i; if (was[i] == t[i]) return; }
    if (!op1f_text(document, name, t, width)) ++st.texts_missed;
    put(was, 0, LINE_ROOM, t);
    st.text_set |= 1u << p;
}
static void show(int p, int on) {                                      // only a change reaches the base
    on = on ? 1 : 0;
    if (!part[p] || (int)st.part_on[p] == on) return;
    st.part_on[p] = (u32)on;
    op1f_show(part[p], on);
}
static void move(int p, int a, int b, int c, int d, int e, int f) {   // [a, b, 0, c, d, 0, e, f, 1024]
    int* m = st.part_m[p];
    if (!part[p] || (m[0] == a && m[1] == b && m[3] == c && m[4] == d && m[6] == e && m[7] == f)) return;
    m[0] = a; m[1] = b; m[2] = 0; m[3] = c; m[4] = d; m[5] = 0; m[6] = e; m[7] = f; m[8] = 1024;
    op1f_move(part[p], m);
}
static void slide(int p, int dx, int dy) { move(p, 1024, 0, 0, 1024, dx, dy); }

// Once, right after the picture: every part found; the start-hidden ones hidden before the page shows. A part missing:
// one log line, and every part found but the fork is hidden (the rows then carry version 5's marks).
static void find_parts() {
    int missing = 0;
    const char* first = "";
    for (int p = 0; p < PARTS; ++p) {
        part[p] = op1f_node(document, PART_NAMES[p]);
        st.part_on[p] = 1;
        st.part_m[p][0] = st.part_m[p][4] = st.part_m[p][8] = 1024;
        if (!part[p]) { if (!missing) first = PART_NAMES[p]; ++missing; }
    }
    st.parts_ok = missing == 0;
    for (int p = 0; p < PARTS; ++p) if (p < START_HIDDEN || (!st.parts_ok && p != P_ICON)) show(p, 0);
    if (missing) op1f_log(2, LOG_FILE, 7, "tuning: picture lacks %d part(s), first '%s': rows marked, no monochord", missing, first);
}

// The parts as DESIGN.md says: cursors, applied dots, bridge + arc + dot at root R and cents C, degree dots, not-stock.
static void dress_parts(int bad) {
    const int ours = st.shows != SHOWS_NOT_STOCK, pending = st.pending != 0;
    show(P_ICON, !bad);
    show(P_WARN, bad);
    show(P_SLACK, !ours);
    show(P_LOCK, !ours);
    slide(P_GCUR, 0, ROW_STEP * (st.group - st.group_top));
    slide(P_TCUR, 0, ROW_STEP * (st.item - st.item_top));
    const int ga = st.active_g - st.group_top, ta = st.active_item - st.item_top;
    const int gon = ours && st.active_g >= 0 && ga >= 0 && ga < (int)ROWS;
    const int ton = gon && st.group == st.active_g && ta >= 0 && ta < (int)ROWS;
    show(P_GACT, gon);
    if (gon) slide(P_GACT, 0, ROW_STEP * ga);
    show(P_TACT, ton);
    if (ton) slide(P_TACT, 0, ROW_STEP * ta);
    const int C = pending ? st.detune : st.active_detune;
    const int R = pending ? (kind_of(current()) == KIND_OFF ? 0 : st.root) : (st.active_kind == KIND_OFF ? 0 : st.active_root);
    const int sg = C > 0 ? 1 : C < 0 ? -1 : 0, ac = C < 0 ? -C : C;
    const int step = BXK[R + 1 + sg] - BXK[R + 1];                         // at most 24356 either way
    const u32 frac = ((u32)(step < 0 ? -step : step) * (u32)ac * 1311u) >> 17;   // |step| * |C| / 100 (1311 / 2^17)
    const int dx = BXK[R + 1] + (step < 0 ? -(int)frac : (int)frac);
    show(P_BRIDGE, ours && !pending);
    show(P_BRIDGEO, ours && pending);
    show(P_ARC, ours);
    show(P_DOT, ours && !pending);
    show(P_DOTO, ours && pending);
    slide(P_BRIDGE, dx, 0);
    slide(P_BRIDGEO, dx, 0);
    slide(P_ARC, dx, 0);
    const int c = COS[ac], s = sg * SIN[ac];
    const int e = BRIDGE_X * (1024 - c) + BRIDGE_Y * s + dx, f = BRIDGE_Y * (1024 - c) - BRIDGE_X * s;
    move(P_DOT, c, s, -s, c, e, f);
    move(P_DOTO, c, s, -s, c, e, f);
    for (int k = 0; k < 12; ++k) {
        show(P_DEG0 + k, ours);
        slide(P_DEG0 + k, -(S_K[k] * st.dev[k]), 0);
    }
}

static void dress(u32 ms) {
    char* t = st.line;
    const int marks = !st.parts_ok;                                        // fallback: version 5's marked rows
    const int bad = st.shows == SHOWS_BAD && (int)(st.bad_until - ms) > 0;   // 'x: not valid' for 3 s, then what plays
    if (st.shows == SHOWS_NOT_STOCK) {
        put(t, 0, LINE_ROOM, st.outside ? "table set by a pack" : "table not stock");   // short: the title ends before ROOT
    } else if (bad) {                                                      // held to the title's room; no root beside it
        put_refused(t);
    } else {
        put_title(t);
    }
    text(0, "tuning_title", t, TITLE_WIDTH);
    t[0] = 0;                                                              // the root letter beside the picture's ROOT; none for 12 equal
    if (st.shows != SHOWS_NOT_STOCK && !bad && st.active_kind != KIND_OFF) put(t, 0, LINE_ROOM, ROOTS[st.active_root]);
    text(1, "tuning_root", t, ROOT_WIDTH);
    put_signed(t, 0, LINE_ROOM, st.stock ? st.detune : st.active_detune);  // the fourth knob at once; not stock: what is in place
    text(2, "tuning_cents", t, CENTS_WIDTH);
    st.group_top = top(st.group_top, st.group, st.groups);
    const int count = st.group_count[st.group];
    st.item_top = top(st.item_top, st.item, count);
    for (int r = 0; r < (int)ROWS; ++r) {
        const int g = st.group_top + r;
        t[0] = 0;
        if (g < st.groups) put_shown(t, marks ? put(t, 0, LINE_ROOM, g == st.group ? MARK : NO_MARK) : 0, LINE_ROOM, st.group_name[g], st.group_len[g]);
        fit(t, LIST_LETTERS, marks ? (int)FALLBACK_GROUPS_PX : (int)GROUP_WIDTH);
        text(3 + r, GROUP_PARTS[r], t, marks ? (int)FALLBACK_GROUPS_PX : (int)GROUP_WIDTH);
        const int k = st.item_top + r;
        t[0] = 0;
        if (k < count) {
            int n = 0;
            const char* s = shown_of(st.entry[st.group_first[st.group] + k], &n);
            put_shown(t, marks ? put(t, 0, LINE_ROOM, k == st.item ? MARK : NO_MARK) : 0, LINE_ROOM, s, n);
            fit(t, LIST_LETTERS, TUNING_WIDTH);
        }
        text(3 + ROWS + r, ROW_PARTS[r], t, TUNING_WIDTH);
    }
    if (st.parts_ok) dress_parts(bad);
}

// ---- once a frame, on the display thread
static void start(u32 ms) {
    for (int n = 0; n < TC_NOTES; ++n) snapshot[n] = TABLE[n];
    st.sum_found = tc_crc32((const u8*)snapshot, 4u * TC_NOTES);
    st.stock = st.sum_found == STOCK_SUM;
    st.table_sum = st.sum_found;
    st.active_kind = KIND_OFF;
    st.seen_group = group_steps;
    st.seen_tuning = tuning_steps;
    st.seen_root = root_steps;
    st.seen_detune = detune_steps;
    if (!st.stock) {
        make_groups();                                                     // the built-ins only; nothing is applied
        st.shows = SHOWS_NOT_STOCK;
        op1f_log(2, LOG_FILE, 1, "tuning: note table sum %08x, stock %08x: not stock, nothing will be written", st.sum_found, STOCK_SUM);
        return;
    }
    look();
    const int at = read_set();
    char remembered[OP1F_FILE_NAME];
    copy_name(remembered, at == -2 ? "" : st.bad_name);
    const int ok = at >= 0 && apply(at, ms);
    if (ok) {
        place(at);
    } else {
        apply(0, ms);                                                      // "12 equal", with the remembered root and detune
        if (at != -2) { st.shows = SHOWS_BAD; st.bad_until = ms + BAD_SHOW_MS; copy_name(st.bad_name, remembered); }   // missing or not valid: say so
    }
    char d[16];
    put_cents(d, 0, sizeof d, st.active_detune);
    op1f_log(3, LOG_FILE, 1, "tuning: note table stock; %d .oft file(s) of %d listed, %d groups; remembered '%s' (%s); now '%s', root %s, %s",
             st.files, st.listed, st.groups, at == -2 ? "nothing" : at == 0 ? "off" : remembered,
             at == -2 ? "no settings" : at == -1 ? "not found" : ok ? "applied" : "not valid",
             st.active_kind == KIND_OFF ? "off" : st.active_name, ROOTS[st.active_root], d);
}

static int take(volatile int* total, int* seen) {
    const int t = *total, moved = t - *seen;
    *seen = t;
    return clamp(moved, -1200, 1200);
}

extern "C" void tuning_frame(const Op1fFrame* now) OP1F_NAMED("tuning_frame");
extern "C" void tuning_frame(const Op1fFrame* now) {
    if (now->bytes < (u32)FRAME_LEVEL_3) return;
    if (!st.started) {
        st.started = 1;
        document = op1f_picture(&op1f_card_tuning, "tuning.svg");    // asked once; kept even when 0 (then no page)
        if (document) find_parts();
        else op1f_log(2, LOG_FILE, 8, "tuning: picture tuning.svg not made: no page, shift + metronome stays the firmware's");
        start(now->ms);
        st.disk = now->disk;
    }
    const u32 shown = now->page == &op1f_card_tuning ? 1u : 0u;
    if (st.stock && st.disk && !now->disk) {                                 // the disk mode closed: list again
        keep_chosen();
        look();                                                              // old file numbers mean nothing now
        find_chosen();
        const int found = find(st.active_kind, st.active_name);
        const int i = find_or_builtin(st.active_kind, st.active_name);      // the active stand-in deleted: its built-in's row
        int item = 0;                                                        // the applied dots' row in the new listing
        st.active_g = st.active_kind == KIND_OFF ? where(0, &item) : where(found, &item);
        st.active_item = st.active_g < 0 ? 0 : item;
        st.pending = 0;
        if (i >= 0) {
            place(i);
            if (found < 0 && kind_of(i) == KIND_BUILTIN) apply(i, now->ms);   // at once, so the deleted file is never saved
            else if (st.active_kind == KIND_FILE || kind_of(i) != st.active_kind) { st.pending = 1; st.rest = 0; }   // read again (the computer
                                                                                 // may have changed it), or a new file took a built-in's row
        }
        keep_in();
    }
    st.disk = now->disk;
    const int dg = take(&group_steps, &st.seen_group), dt = take(&tuning_steps, &st.seen_tuning);
    const int dr = take(&root_steps, &st.seen_root), dd = take(&detune_steps, &st.seen_detune);
    if (dg || dt || dr || dd) {
        if (dg) {
            const int g = clamp(st.group + dg, 0, st.groups - 1);
            if (g != st.group) { st.group = g; st.item = st.group_chosen[g]; }
        }
        keep_in();
        if (dt) {
            st.item = clamp(st.item + dt, 0, st.group_count[st.group] - 1);
            st.group_chosen[st.group] = st.item;
        }
        int r = st.root + dr;
        while (r < 0) r += 12;
        while (r > 11) r -= 12;
        st.root = r;
        st.detune = clamp(st.detune + dd, -TC_DETUNE_MOST, TC_DETUNE_MOST);
        st.pending = 1;
        st.rest = 0;
    } else if (st.pending && ++st.rest >= (u32)REST_FRAMES) {
        st.pending = 0;
        apply(current(), now->ms);
    }
    if (shown && st.stock && !st.pending && (now->frame & (LOOK_FRAMES - 1)) == 0) table_ours();   // a change from outside shows
    if (shown && !st.shown && st.shows == SHOWS_BAD) st.bad_until = now->ms + BAD_SHOW_MS;   // opened later: show it again
    if (shown) dress(now->ms);
    st.shown = shown;
    save(now->ms, now->disk);
    int why = quiet_popup(&st.quiet, now->popup != 0);
    int period = --st.countdown <= 0;
    if (!period && !why) return;
    if (period) {
        st.countdown = PERIOD_FRAMES;
        u32 s = quiet_mix(quiet_start(), st.applied);
        s = quiet_mix(s, st.refused);
        s = quiet_mix(s, (u32)current());
        s = quiet_mix(s, keys);
        s = quiet_mix(s, turns);
        s = quiet_mix(s, st.saves + st.not_now + st.save_failed);
        s = quiet_mix(s, (u32)st.looks);
        why |= quiet_period(&st.quiet, s, shown);
    }
    if (!why) return;
    char d[16];
    put_cents(d, 0, sizeof d, st.active_detune);
    op1f_log(3, LOG_FILE, 4, "tuning: %s; now '%s', root %s, %s; %d groups, %d files, group %d tuning %d; %u applied, %u refused; saved %u, %u not now, %u failed; %u keys, %u turns, %u text(s) not set",
             st.stock ? "stock at start" : "NOT stock", st.active_kind == KIND_OFF ? "off" : st.active_name, ROOTS[st.active_root], d,
             st.groups, st.files, st.group, st.item, st.applied, st.refused, st.saves, st.not_now, st.save_failed, keys, turns, st.texts_missed);
    if (st.quiet.silent_before)
        op1f_log(3, LOG_FILE, 5, "tuning quiet: these lines after %u periods held (%s); %u printed, %u held", st.quiet.silent_before, quiet_why(why),
                 st.quiet.spoken, st.quiet.skipped);
}
