/*
 * cmatrix.exe — Matrix digital rain for Windows Terminal / ConHost
 * Requires Windows 10+ (ENABLE_VIRTUAL_TERMINAL_PROCESSING)
 * Build: gcc -O2 -o cmatrix.exe cmatrix.c
 *
 * Any key exits. Options: -b bold | -B all-bold | -c katakana | -r rainbow
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <conio.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

/* ── tunables ─────────────────────────────────────────────── */
#define BASE_FRAME_MS   40
#define BRIGHT_ZONE      4
#define MAX_ROWS       512

/* ── ANSI helpers ─────────────────────────────────────────── */
#define A_HIDE_CUR "\033[?25l"
#define A_SHOW_CUR "\033[?25h"
#define A_CLR_SCR  "\033[2J\033[H"
#define A_RESET    "\033[0m\033[39m\033[49m"

/* ── color sets ───────────────────────────────────────────── */
/* index 0 = green (default), 1-6 = rainbow colors */
static const char *COL_HEAD   = "\033[1;97m";   /* always white */
static const char *COL_BRIGHT[] = {
    "\033[1;32m",  /* green  */
    "\033[1;31m",  /* red    */
    "\033[1;33m",  /* yellow */
    "\033[1;34m",  /* blue   */
    "\033[1;35m",  /* magenta*/
    "\033[1;36m",  /* cyan   */
    "\033[1;37m",  /* white  */
};
static const char *COL_NORMAL[] = {
    "\033[32m",
    "\033[31m",
    "\033[33m",
    "\033[34m",
    "\033[35m",
    "\033[36m",
    "\033[37m",
};
static const char *COL_DIM[] = {
    "\033[2;32m",
    "\033[2;31m",
    "\033[2;33m",
    "\033[2;34m",
    "\033[2;35m",
    "\033[2;36m",
    "\033[2;37m",
};
#define N_COLORS 7

/* ── character pools ──────────────────────────────────────── */
static const char ASCII_POOL[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    "@#$%&*+=<>:;!?|/\\{}[]~^";
#define ASCII_POOL_LEN ((int)(sizeof(ASCII_POOL) - 1))

/* Half-width katakana: U+FF65 .. U+FF9F (59 codepoints, all 3-byte UTF-8) */
#define KATA_FIRST 0xFF65
#define KATA_COUNT  59

/* ── globals ──────────────────────────────────────────────── */
static int   g_w, g_h;
static int   g_bold      = 0;   /* 1 = bold heads+bright, 2 = all bold */
static int   g_katakana  = 0;
static int   g_rainbow   = 0;
static int   g_speed     = 5;
static int   g_frame_ms  = BASE_FRAME_MS;
static DWORD g_orig_mode = 0;

/* ── cleanup / signal ─────────────────────────────────────── */
static void cleanup(void)
{
    printf(A_SHOW_CUR A_CLR_SCR A_RESET);
    fflush(stdout);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), g_orig_mode);
}

static BOOL WINAPI ctrl_handler(DWORD ev)
{
    (void)ev;
    cleanup();
    return FALSE;
}

/* ── console init ─────────────────────────────────────────── */
static void enable_vt(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(h, &g_orig_mode);
    SetConsoleMode(h, g_orig_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
    SetConsoleOutputCP(65001);   /* UTF-8 output for katakana */
}

static void get_size(void)
{
    CONSOLE_SCREEN_BUFFER_INFO i;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &i);
    g_w = i.srWindow.Right  - i.srWindow.Left + 1;
    g_h = i.srWindow.Bottom - i.srWindow.Top  + 1;
    if (g_w < 1) g_w = 80;
    if (g_h < 1) g_h = 24;
}

/* ── character generation ─────────────────────────────────── */
static void rchar(char *buf)
{
    if (g_katakana) {
        int cp = KATA_FIRST + rand() % KATA_COUNT;
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        buf[3] = '\0';
    } else {
        buf[0] = ASCII_POOL[rand() % ASCII_POOL_LEN];
        buf[1] = '\0';
        buf[2] = '\0';
        buf[3] = '\0';
    }
}

/* ── per-column state ─────────────────────────────────────── */
typedef struct {
    int  head;
    int  len;
    int  speed;
    int  tick;
    int  delay;
    int  color_idx;           /* index into COL_* arrays */
    char glyph[MAX_ROWS][4];  /* UTF-8 char per row (up to 3 bytes + NUL) */
} Stream;

static Stream *g_streams;

static void stream_reset(Stream *s)
{
    s->head      = -1;
    s->len       = g_h / 4 + rand() % (3 * g_h / 4 + 1);
    s->speed     = 1 + rand() % 3;
    s->tick      = 0;
    s->delay     = rand() % 120;
    s->color_idx = g_rainbow ? (rand() % N_COLORS) : 0;
    for (int r = 0; r < MAX_ROWS; r++) {
        s->glyph[r][0] = ' ';
        s->glyph[r][1] = '\0';
    }
}

static void streams_init(void)
{
    if (g_streams) free(g_streams);
    g_streams = calloc(g_w, sizeof(Stream));
    if (!g_streams) { fputs("out of memory\n", stderr); exit(1); }
    for (int c = 0; c < g_w; c++) stream_reset(&g_streams[c]);
}

/* ── rendering ────────────────────────────────────────────── */
static inline void put_cell(int col, int row, const char *ch, const char *ansi)
{
    printf("\033[%d;%dH%s%s", row + 1, col + 1, ansi, ch);
}

static inline void erase_cell(int col, int row)
{
    printf("\033[%d;%dH\033[0m ", row + 1, col + 1);
}

static void frame_update(void)
{
    for (int c = 0; c < g_w; c++) {
        Stream *s = &g_streams[c];

        if (s->delay > 0) { s->delay--; continue; }
        s->tick++;
        if (s->tick < s->speed) continue;
        s->tick = 0;

        if (s->head < 0) s->head = 0;
        int h   = s->head;
        int ci  = s->color_idx;

        const char *c_bright = COL_BRIGHT[ci];
        const char *c_normal = (g_bold == 2) ? COL_BRIGHT[ci] : COL_NORMAL[ci];
        const char *c_dim    = COL_DIM[ci];

        /* new head: white */
        if (h < g_h) {
            rchar(s->glyph[h]);
            put_cell(c, h, s->glyph[h], COL_HEAD);
        }

        /* one behind head: bright */
        if (h - 1 >= 0 && h - 1 < g_h) {
            if (rand() % 8 == 0) rchar(s->glyph[h-1]);
            put_cell(c, h - 1, s->glyph[h-1], c_bright);
        }

        /* bright→normal transition */
        int tr = h - BRIGHT_ZONE - 1;
        if (tr >= 0 && tr < g_h) {
            if (rand() % 20 == 0) rchar(s->glyph[tr]);
            put_cell(c, tr, s->glyph[tr], c_normal);
        }

        /* dim zone near tail */
        int dim = h - s->len + 3;
        if (dim >= 0 && dim < g_h && dim > tr) {
            put_cell(c, dim, s->glyph[dim], c_dim);
        }

        /* erase tail */
        int tail = h - s->len;
        if (tail >= 0 && tail < g_h) {
            s->glyph[tail][0] = ' ';
            s->glyph[tail][1] = '\0';
            erase_cell(c, tail);
        }

        s->head++;
        if (s->head - s->len > g_h) {
            stream_reset(s);
            s->delay = rand() % 40;
        }
    }

    fflush(stdout);
}

/* ── speed ────────────────────────────────────────────────── */
static void apply_speed(void)
{
    g_frame_ms = (190 - g_speed * 20) * 2 / 3;
    if (g_frame_ms < 10)  g_frame_ms = 10;
    if (g_frame_ms > 200) g_frame_ms = 200;
}

/* ── entrypoint ───────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-b")) {
            if (g_bold < 1) g_bold = 1;
        } else if (!strcmp(argv[i], "-B")) {
            g_bold = 2;
        } else if (!strcmp(argv[i], "-c")) {
            g_katakana = 1;
        } else if (!strcmp(argv[i], "-r")) {
            g_rainbow = 1;
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            g_speed = atoi(argv[++i]);
            if (g_speed < 1) g_speed = 1;
            if (g_speed > 9) g_speed = 9;
        } else if (!strcmp(argv[i], "-u") && i + 1 < argc) {
            int d = atoi(argv[++i]);
            if (d < 0) d = 0;
            if (d > 10) d = 10;
            g_speed = 10 - d;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf(
                "cmatrix — Matrix digital rain\n"
                "Usage: cmatrix [-b] [-B] [-c] [-r] [-s speed] [-u delay]\n"
                "  -b        bold heads\n"
                "  -B        all bold (overrides -b)\n"
                "  -c        katakana characters (needs UTF-8 font)\n"
                "  -r        rainbow colors\n"
                "  -s <1-9>  speed (1=slow, 9=fast, default 5)\n"
                "  -u <0-10> update delay (0=fast, 10=slow)\n"
                "Press any key to exit.\n"
            );
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s  (try -h)\n", argv[i]);
            return 1;
        }
    }

    srand((unsigned)time(NULL));
    apply_speed();
    enable_vt();
    get_size();

    static char out_buf[1 << 20];
    setvbuf(stdout, out_buf, _IOFBF, sizeof(out_buf));

    printf(A_HIDE_CUR A_CLR_SCR);
    fflush(stdout);

    streams_init();

    while (1) {
        if (_kbhit()) {
            _getch();
            break;   /* any key exits */
        }
        frame_update();
        Sleep(g_frame_ms);
    }

    cleanup();
    free(g_streams);
    return 0;
}
