"""More than one player on more than one Npad (host/nn_hid.c's players), against fake controllers.

The player layer, the per-player read, the script's pad index, the style set and the vibration routing are extracted
unmodified from production and compiled. The controllers, clock, keyboard, mouse and window are fakes; no real device
or game is touched. Each case runs in a process of its own, because production reads HOS_NPAD_PLAYERS once.

What this pins down:
  ids       HOS_NPAD_PLAYERS=4 serves No.1..No.4 and nothing else; a player is connected only while its controller
            is, except No.1, which is connected without one so the keyboard can always play. Styles follow.
  auto      HOS_NPAD_PLAYERS=auto is one player per attached controller, and at least one.
  own-pad   each player reads its own controller, not the merge of all of them; the keyboard and mouse stay with
            player 1; the Insert menu's Back+Start chord reaches no player.
  script    "P2" in a press sends it to No.2 alone, and a press without a P is player 1's as before.
  rumble    a vibration value goes to the pad its handle names, not to every pad.

The single-player path - HOS_NPAD_PLAYERS unset, everything merged into No.1, which is what Ocarina reads - is
tests/test_input_background.py's, and it asserts the opposite of this at the same boundary: that production never asks
for a per-player read.

python -I -B tests/test_multi_pad.py [--cc gcc]
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))   # (tests/srccut.py: python -I leaves this folder off the path)
from srccut import block   # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
RUN_ALL_ARGS = ["--cc", "gcc"]
CASES = ("ids", "auto", "own-pad", "script", "rumble")
PLAYERS = {"ids": "4", "auto": "auto", "own-pad": "2", "script": "3", "rumble": "2"}
SCRIPT = {"script": "0:A+LUP/200,0:P2+B/200"}


# (env_on, the switches' rule, before the code cut out of the sources: gpu/vk/compat/hookshot_env.h)
ENV_ON_H = str(ROOT / "gpu" / "vk" / "compat" / "hookshot_env.h")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cc", default="gcc")
    p.add_argument("--case", choices=CASES)
    a = p.parse_args()
    hid = (ROOT / "host" / "nn_hid.c").read_text(encoding="utf-8")
    api = (ROOT / "gpu" / "vk" / "vk_api.h").read_text(encoding="utf-8")

    constants = block(hid, "enum { B_A") + ";\n" + block(api, "enum { HOS_SW_A") + ";\n"
    constants += block(api, "enum { HOS_PAD_SOUTH") + ";\n" + block(api, "enum { HOS_KEY_MOVE_UP") + ";\n"
    constants += block(api, "enum { HOS_OPT_CHEAT_HEARTS") + ";\n"   # (Ocarina's debug tools, which npad_states reads)
    constants += block(api, "enum { HOS_BIND_PAD") + ";\n"   # (the live input's !bind)
    constants += block(api, "enum { HOS_OPT_AUTO_RUN") + ";\n"   # (Auto run: off here, rx_option answers 0)
    constants += (ROOT / "host/auto_run.h").read_text(encoding="utf-8") + "\n"
    for name in ("STATE_SIZE", "NPAD_NO1", "STYLE_FULLKEY", "ATTR_CONNECTED", "NPAD_PLAYERS_MAX",
                 "RUMBLE_HEAVY_TOP", "RUMBLE_LIGHT_BOTTOM"):
        matches = re.findall(r"^#define " + name + r"\s+[^\n]+", hid, re.M)
        if len(matches) != 1:
            raise SystemExit("host/nn_hid.c: %s is not defined exactly once" % name)
        constants += matches[0] + "\n"

    # (the script parser and its presses, which the pad index lives in)
    start = hid.index("typedef struct { int64_t start_ms, end_ms;")
    script = block(hid, "static void scripted_input(")
    presses = hid[start:hid.index(script) + len(script)]
    if "p.pad = s[1] - '1'" not in presses:
        raise SystemExit("host/nn_hid.c: a script press no longer carries a pad index")
    # Ocarina's debug tools live in npad_states, and this fork gates them on player 1. Their state and their
    # callbacks are extracted and spied so the cases below prove the debug path stays off: the fixture's rx_option
    # answers -1 for the flight option, which is "no debug tools", and any call into them fails the case.
    cheat_state = re.search(r"^static uint64_t flight_input;\nstatic uint64_t free_camera_axes;\n"
                            r"static int free_camera_height;\nstatic int move_stick_held;", hid, re.M).group(0)
    cheat_spies = """
volatile int hos_oot_live_action;
int hos_oot_flight_shortcut_ready(void) { CHECK(!"unexpected cheat readiness query"); return 0; }
void rx_option_set(int id, int value) { (void)id; (void)value; CHECK(!"unexpected cheat option change"); }
int hos_oot_player_free(void) { return 1; }
"""
    # (the host's own presses and stick, for the game's menus' mouse (host/oot_menu.c): none here; the player's input
    # time it hides the mouse by: not looked at)
    host_spies = """
static void host_taps(void) {}
static void host_stick_read(int32_t stick[4]) { (void)stick; }
static void host_drag_read(int32_t stick[4]) { (void)stick; }
static void player_used(void) {}
"""
    actual = "\n".join((
        cheat_state, cheat_spies, host_spies,
        block(hid, "static int key("), block(hid, "static int kmap_down("), block(hid, "static int game_in_front("),
        presses, block(hid, "static void stick_max("), block(hid, "static int pads_in_background("),
        block(hid, "static int npad_players("), block(hid, "static int npad_player("),
        block(hid, "static int npad_player_connected("),
        block(hid, "static uint64_t bound_pad_buttons("), block(hid, "static uint64_t pads_mapped("),
        block(hid, "static int pads_read_one("), block(hid, "static int pads_merge("), block(hid, "static int pads_behind("),
        block(hid, "static int mouse_vk_slot("), re.search(r"^static int mouse_armed\[5\];", hid, re.M).group(0),
        block(hid, "static void mouse_buttons_arm("), block(hid, "static int bound_down("),
        block(hid, "static void oc_notes("), block(hid, "static uint64_t oc_pad_word("),
        block(hid, "static int flight_jump_shortcut("),
        block(hid, "static void read_input("), block(hid, "static void npad_states("),
        block(hid, "HLE(_ZN2nn3hid15GetNpadStyleSetERKj)"),
        # (the definition, not the forward declaration above npad_states)
        block(hid, "static float rumble_motor("), block(hid, "static float rumble_heavy_share("),
        block(hid, "static void rumble_map("),
        block(hid, "static void vibration_send(uint64_t handle, uint64_t value) {")))
    # production reads one controller per player: a merged read in the per-player path would defeat the whole point
    per_player = block(hid, "static int pads_read_one(")
    if "hos_sdl_pad_state(" not in per_player:
        raise SystemExit("host/nn_hid.c: a player's own controller is no longer read through pad_sdl.c")
    states = block(hid, "static void npad_states(")
    if "if (connected && player == 0) {" not in states:
        raise SystemExit("host/nn_hid.c: Ocarina's debug tools are no longer the first player's alone")

    env = {k: v for k, v in os.environ.items()
           if not k.startswith("HOS_INPUT_") and k not in ("HOS_HEADLESS", "HOS_WINDOW_NOACTIVATE",
                                                           "HOS_NPAD_PLAYERS")}
    flags = subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS if os.name == "nt" else 0

    def run(command, environment=None):
        result = subprocess.run(command, env=environment, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", creationflags=flags)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        result.check_returncode()

    with tempfile.TemporaryDirectory(prefix="multi-pad-") as tmp:
        folder = Path(tmp).resolve()
        (folder / "multi_pad_constants.inc").write_text(constants, encoding="utf-8")
        (folder / "multi_pad_actual.inc").write_text(actual, encoding="utf-8")
        exe = folder / "fixture.exe"
        # Production's button table deliberately leaves its unused axis fields zero-initialized.
        run([a.cc, "-std=c11", "-include", ENV_ON_H, "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers",
             "-D_CRT_SECURE_NO_WARNINGS", "-I" + str(folder),
             str(ROOT / "tests/multi_pad_test.c"), "-o", str(exe), "-lm"])
        for case in ((a.case,) if a.case else CASES):
            case_env = dict(env, HOS_NPAD_PLAYERS=PLAYERS[case])
            if case in SCRIPT:
                case_env["HOS_INPUT_SCRIPT"] = SCRIPT[case]
            run([str(exe), case], case_env)


if __name__ == "__main__":
    main()
