/* Authored memory, clock and controller spies; multi_pad_actual.inc holds the unmodified production path out of
   host/nn_hid.c. Nothing real is opened: the controllers are fake SDL slots this file owns, read through the same two
   functions host/pad_sdl.c exports. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL:%d %s\n", __LINE__, #x); exit(1); } } while (0)
#include "multi_pad_constants.inc"
/* API spies must not collide with Windows SDK / import-library names. */
#define GetCurrentProcessId fixture_GetCurrentProcessId
#define InterlockedCompareExchange64 fixture_InterlockedCompareExchange64
#define InterlockedExchange fixture_InterlockedExchange
#define InterlockedOr fixture_InterlockedOr

typedef void *HWND;
typedef uint32_t DWORD;
typedef intptr_t LONG_PTR;
typedef int32_t LONG;
typedef int64_t LONG64;
typedef struct { uint64_t x[32]; } Ctx;
#define HLE(sym) void imp_##sym(Ctx *c)
enum { GWL_EXSTYLE = -20, WS_EX_TOOLWINDOW = 0x80, WS_EX_NOACTIVATE = 0x8000000,
       VK_MENU = 0x12, VK_RETURN = 0x0d, VK_LEFT = 0x25, VK_UP, VK_RIGHT, VK_DOWN,
       VK_LBUTTON = 1, VK_RBUTTON = 2, VK_MBUTTON = 4, VK_XBUTTON1 = 5, VK_XBUTTON2 = 6 };

volatile LONG64 hos_present_count = 1000;
volatile int64_t hos_hid_last_read;
volatile int hos_nvn_sync_log_on;
static int64_t now_ns = 10000000000LL, sampling;
static int front = 1, merges, key_queries, mouse_queries;
static volatile LONG pad_raw_now;
static int key_bind[HOS_KEY_N][2];
static volatile LONG pad_bind[HOS_SW_N];
static unsigned char *memory;
static char last_log[256];
static int pads_read_one(int slot, uint64_t *buttons, int32_t stick[4]);   /* production's, in the .inc below */

LONG64 InterlockedCompareExchange64(volatile LONG64 *p, LONG64 value, LONG64 expected) {
    const LONG64 old = *p;
    if (old == expected) *p = value;
    return old;
}
LONG InterlockedExchange(volatile LONG *p, LONG v) { const LONG old = *p; *p = v; return old; }
LONG InterlockedOr(volatile LONG *p, LONG v) { const LONG old = *p; *p |= v; return old; }
static void *at(uint64_t address) { CHECK(address <= 512 - 8); return memory + address; }
#define GP(a) at(a)
#define G32(a) (*(uint32_t *)at(a))
#define G64(a) (*(uint64_t *)at(a))
#define ARG(n) (c->x[n])
#define RET(value) (c->x[0] = (uint64_t)(value))

int rx_option(int id, int otherwise) { return id == 1 ? 0 : otherwise; }
int64_t hos_now_ns(void) { return now_ns; }
void hos_log(const char *format, ...) {
    va_list args; va_start(args, format); vsnprintf(last_log, sizeof last_log, format, args); va_end(args);
}
HWND GetForegroundWindow(void) { return (HWND)(uintptr_t)(front ? 1 : 2); }
DWORD GetWindowThreadProcessId(HWND window, DWORD *pid) { *pid = window == (HWND)(uintptr_t)1 ? 10 : 20; return 1; }
DWORD GetCurrentProcessId(void) { return 10; }
LONG_PTR GetWindowLongPtrA(HWND window, int index) { (void)window; (void)index; return 0; }
int GetClassNameA(HWND window, char *text, int size) { (void)window; return snprintf(text, (size_t)size, "fixture"); }
/* K is down and nothing else is: binds_load below binds it to X, so it shows which player the keyboard reaches */
short GetAsyncKeyState(int key) { key_queries++; return key == 'K' ? (short)-32768 : 0; }

/* ---- the fake controllers ---------------------------------------------------------------------------------------- */
static struct { int open; uint64_t buttons; int32_t stick[4]; } slots[NPAD_PLAYERS_MAX];
static int rumble_slot = -1, rumble_all;
static float rumble_low_sent, rumble_high_sent;

int hos_sdl_pad_count(void) {
    int n = 0;
    for (int i = 0; i < NPAD_PLAYERS_MAX; i++) n += slots[i].open ? 1 : 0;
    return n;
}
int hos_sdl_pad_state(int slot, uint64_t *buttons, int32_t stick[4]) {
    CHECK(slot >= 0 && slot < NPAD_PLAYERS_MAX);
    if (!slots[slot].open) return 0;
    *buttons |= slots[slot].buttons;
    for (int k = 0; k < 4; k++)
        if ((slots[slot].stick[k] < 0 ? -slots[slot].stick[k] : slots[slot].stick[k])
            > (stick[k] < 0 ? -stick[k] : stick[k]))
            stick[k] = slots[slot].stick[k];
    return 1;
}
void hos_sdl_rumble_pad(int slot, float low, float high) {
    rumble_slot = slot; rumble_low_sent = low; rumble_high_sent = high;
}
void hos_sdl_rumble(float low, float high) { rumble_all = 1; rumble_low_sent = low; rumble_high_sent = high; }
int hos_sdl_pads(uint64_t *buttons, int32_t stick[4]) { (void)buttons; (void)stick; return hos_sdl_pad_count(); }

/* The one dispatch line this fixture stands in for. Every case here serves more than one player, so production must
   ask for a per-player read and never for the merge; the merged branch is test_input_background.py's, where the
   same boundary asserts the opposite (slot < 0). */
void pads_read(int slot, uint64_t *buttons, int32_t stick[4]) {
    CHECK(slot >= 0);
    merges++;
    pads_read_one(slot, buttons, stick);
}
void binds_load(void) {
    for (int i = 0; i < HOS_SW_N; i++) pad_bind[i] = -1;
    pad_bind[HOS_SW_A] = HOS_PAD_EAST;      /* (the shipped defaults for the buttons these cases press) */
    pad_bind[HOS_SW_B] = HOS_PAD_SOUTH;
    pad_bind[HOS_SW_PLUS] = HOS_PAD_START;
    pad_bind[HOS_SW_MINUS] = HOS_PAD_BACK;
    key_bind[HOS_SW_X][0] = 'K';
}
int hos_mouse_native(void) { return -1; }
static void mouse_setup(void) {}
static void mouse_sens_keys(void) {}
int mouse_stick(int32_t *x, int32_t *y, int held) { (void)x; (void)y; (void)held; mouse_queries++; return 0; }
void rx_request_frame_dump(void) {}
void rx_set_screenshot_ms(int64_t ms) { (void)ms; }
void rx_request_rdoc_capture(int frames) { (void)frames; }
void rx_flash_dark_arm(void) {}
uint64_t hos_title_input(uint64_t buttons) { return buttons; }
uint64_t hos_frame_step_buttons(uint64_t buttons) { return buttons; }

#include "multi_pad_actual.inc"

/* One GetNpadStates call for an npad id, giving back the state it wrote. */
static void read_npad(uint32_t id, uint64_t *buttons, int32_t stick[4], int *connected) {
    Ctx context = {{0}};
    context.x[0] = 64; context.x[1] = 1; context.x[2] = 16; context.x[30] = 0x1234;
    G32(16) = id;
    memset(memory + 64, 0xa5, STATE_SIZE);
    npad_states(&context);
    CHECK(context.x[0] == 1);
    *buttons = G64(64 + 8);
    memcpy(stick, memory + 64 + 16, 16);
    *connected = G32(64 + 32) == ATTR_CONNECTED;
}

static uint32_t style_of(uint32_t id) {
    Ctx context = {{0}};
    context.x[0] = 16;
    G32(16) = id;
    imp__ZN2nn3hid15GetNpadStyleSetERKj(&context);
    return (uint32_t)context.x[0];
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    memory = calloc(512, 1); CHECK(memory);
    const char *name = argv[1];
    uint64_t b = 0;
    int32_t s[4] = {0};
    int connected = 0;

    if (!strcmp(name, "ids")) {
        /* HOS_NPAD_PLAYERS=4: No.1..No.4 are served, No.5 and Handheld (0x20) are not. Only the slots with a
           controller report themselves connected - except No.1, which is connected with no controller at all,
           because the keyboard plays without one. */
        CHECK(npad_players() == 4);
        CHECK(npad_player(0) == 0 && npad_player(3) == 3);
        CHECK(npad_player(4) == -1 && npad_player(0x20) == -1);
        slots[1].open = 1;
        CHECK(npad_player_connected(0) == 1);
        CHECK(npad_player_connected(1) == 1);
        CHECK(npad_player_connected(2) == 0);
        CHECK(npad_player_connected(-1) == 0);
        read_npad(0, &b, s, &connected); CHECK(connected);
        read_npad(1, &b, s, &connected); CHECK(connected);
        read_npad(2, &b, s, &connected); CHECK(!connected && b == 0);
        read_npad(4, &b, s, &connected); CHECK(!connected && b == 0 && s[0] == 0);
        /* the style follows the connection: FullKey for a pad that is there, an empty set for one that is not */
        CHECK(style_of(0) == STYLE_FULLKEY);
        CHECK(style_of(1) == STYLE_FULLKEY);
        CHECK(style_of(2) == 0);
        CHECK(style_of(4) == 0 && style_of(0x20) == 0);
        slots[1].open = 0;   /* unplugged: the game sees it go */
        CHECK(npad_player_connected(1) == 0);
        CHECK(style_of(1) == 0);
        read_npad(1, &b, s, &connected); CHECK(!connected);
    } else if (!strcmp(name, "auto")) {
        /* HOS_NPAD_PLAYERS=auto: as many players as there are controllers, and at least one, so the keyboard always
           has an npad to press. */
        CHECK(npad_players() == 1);   /* none attached */
        slots[0].open = slots[1].open = slots[2].open = 1;
        CHECK(npad_players() == 3);
        CHECK(npad_player(2) == 2 && npad_player(3) == -1);
        slots[2].open = 0;
        CHECK(npad_players() == 2);
        CHECK(npad_player(2) == -1);
    } else if (!strcmp(name, "own-pad")) {
        /* Each player reads its own controller and not the merge: pad 2's A presses nothing on No.1, and the sticks
           do not cross either. */
        slots[0].open = slots[1].open = 1;
        slots[0].buttons = 1ULL << HOS_PAD_EAST;      /* A on pad 1 */
        slots[0].stick[0] = 20000;
        slots[1].buttons = 1ULL << HOS_PAD_SOUTH;     /* B on pad 2 */
        slots[1].stick[1] = -30000;
        read_npad(0, &b, s, &connected);
        CHECK(connected && (b & (1ULL << HOS_SW_A)) && !(b & (1ULL << HOS_SW_B)));
        CHECK(s[0] == 20000 && s[1] == 0);
        read_npad(1, &b, s, &connected);
        CHECK(connected && b == (1ULL << HOS_SW_B));   /* its pad alone: no keyboard, no other pad */
        CHECK(s[0] == 0 && s[1] == -30000);
        /* the keyboard is player 1's: K is bound to X, and player 2 must not even have a key looked at for it */
        read_npad(0, &b, s, &connected);
        CHECK(b & (1ULL << HOS_SW_X));
        const int keys_after_player_one = key_queries;
        read_npad(1, &b, s, &connected);
        CHECK(!(b & (1ULL << HOS_SW_X)));
        CHECK(key_queries == keys_after_player_one);
        CHECK(mouse_queries > 0);   /* the mouse was read for player 1 */
        /* Back and Start together are the Insert menu's chord and reach no player */
        slots[1].buttons = (1ULL << HOS_PAD_BACK) | (1ULL << HOS_PAD_START);
        read_npad(1, &b, s, &connected);
        CHECK(connected && !(b & ((1ULL << HOS_SW_PLUS) | (1ULL << HOS_SW_MINUS))));
        /* every read asked for a slot, never for the merged state */
        CHECK(merges > 0);
    } else if (!strcmp(name, "script")) {
        /* P<n> in a script press names the player it is for, and a press without one is P1, as every script written
           before this meant. With no Kirby build this is the only way the per-player path can be driven at all. */
        slots[0].open = slots[1].open = slots[2].open = 1;
        read_npad(0, &b, s, &connected);
        CHECK(b & (1ULL << HOS_SW_A));                       /* "0:A" with no P went to player 1 */
        CHECK(s[1] == 32767);                                /* and so did its LUP */
        read_npad(1, &b, s, &connected);
        CHECK(b == (1ULL << HOS_SW_B));                      /* "0:P2+B" went to player 2 alone */
        CHECK(s[1] == 0);
        read_npad(2, &b, s, &connected);
        CHECK(b == 0);                                       /* and to nobody else */
        CHECK(npresses == 2 && presses[0].pad == 0 && presses[1].pad == 1);
    } else if (!strcmp(name, "rumble")) {
        /* The vibration handle carries the npad id its value is for, so each pad gets its own rumble instead of every
           pad shaking together. */
        slots[0].open = slots[1].open = 1;
        float value[4] = {1.0f, 160.0f, 0.0f, 320.0f};
        memcpy(memory + 128, value, sizeof value);
        G32(64) = 3u | (1u << 8);          /* style FullKey, npad id 1 (No.2), device 0 */
        vibration_send(64, 128);
        CHECK(rumble_slot == 1 && !rumble_all && rumble_low_sent > 0.5f);
        rumble_slot = -1;
        value[0] = 0.0f;                   /* silence for No.1: it must not undo No.2's */
        memcpy(memory + 128, value, sizeof value);
        G32(64) = 3u;                      /* npad id 0 (No.1) */
        vibration_send(64, 128);
        CHECK(rumble_slot == 0 && !rumble_all && rumble_low_sent == 0.0f);
    } else {
        CHECK(0);
    }
    printf("PASS: actual multi-pad %s\n", name);
    free(memory);
    return 0;
}
