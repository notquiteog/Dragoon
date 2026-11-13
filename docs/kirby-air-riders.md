# Kirby Air Riders on this engine: what is here, what is missing

**Dragoon** is this fork's name, as Hookshot is the Ocarina port's and BlueShell is mkw-recomp's.

This is a survey, written before any code in this branch changed. It asks one question: if a *Kirby Air Riders*
(Switch 2) dump were put in front of this engine, what would it already do, and what would have to be built?

**Correction, 2026-11-13: there is a Kirby dump on this machine.** The survey said there was none because it searched
`/` only; the `.nup` sits on the second drive, where the Eglantine emulator reads it (§8). Sections 1 to 7 are the
survey as written, with the table in §1 brought up to date; §8 is what the dump has said since. Game data still never
enters this repository: the dump is read where it lies, and the extracted modules and the lift stay in the gitignored
`work/`. Where a value would have to come from the game, this document says how it was found, or that it has not been
— it is never guessed at. The evidence that the shared engine is sound is still that Ocarina builds and its checks
pass.

The lineage matters for reading this: mkw-recomp (Mario Kart World) → Hookshot (Ocarina of Time) → this branch.
Mario Kart World is a four-player racer with online play, so some of what Kirby needs was once exercised by MKW and
is still here; the Ocarina retarget is what narrowed the input path to one pad.


## 1. Summary

| | State | Cost |
|---|---|---|
| Switch 2 lift (ARMv8.2, DT_RELR, ZBIC, NVN2 231.x, SM86) | **there**, game-neutral | — |
| Vulkan replay of whatever the game submits | **there** | — |
| Horizon runtime: threads, SVCs, memory, fs, save data, audio, vi, gs, fibers | **there** | — |
| Four local pads (npad No.1..No.4: ids, styles, connection) | **was one pad**; this branch serves up to 8 | done (§3) |
| Per-pad rumble | **was global**; this branch routes by npad id | done (§3) |
| `ldn` (local wireless) | **answered `0` = success by the generic stub**: a lie | this branch refuses loudly (§4) |
| Online play (`nifm`, `socket`, `ssl`, `account` network service) | deliberately offline, failures returned | a project, out of scope |
| Game-specific layer (patches, graphics-module offsets, upscaler key) | **was hardcoded to Ocarina**; now `nx2/game_profile.py`, chosen by main's build id | done (§5) |
| Four player profiles (`nn::account`) | **was one user**; now 1..8 users, the profile's count (Kirby: 4), and a user selector | done (§6) |
| Split-screen / four viewports in the renderer | nothing to do: the replay follows the game | — |
| Build gate on Ocarina's build id | **was hardcoded**; now every build a profile knows (Ocarina's, Kirby's) | done (§5) |
| A Kirby dump | **there**: v1.3.3, read in place | §8 |
| Kirby's lift: instruction coverage | **100.0000 %** of 26.5 M instructions, 279,081 functions | done (§8) |
| Kirby's one TLS descriptor (DLSS library) | loader refused it; now bound to a named stop | the TLS itself is open (§8) |
| Kirby's graphics-module offsets, clock return address, upscaler keys, patches | not found yet: the profile says so and turns those features off | needs runs (§8) |
| Kirby build and boot | **builds**; boots to `nnMain`, hid and the NVN device, then crashes in a job worker | §8: the next wall |

The short version, as written: **the engine is not the problem, the game-specific layer is.** Four-player input was
a day's work and is in this branch. `ldn` is a project and is not attempted. Since then the game-specific layer has a
seam (§5), and the dump has been lifted (§8): what blocks a boot now is listed there.


## 2. What this engine already provides that Kirby needs

Read `docs/design.md` for the pipeline. What is relevant to a second Switch 2 game:

- **The lift is game-neutral.** `nx2/nup.py` takes any `.nup`, `nx2/dump.py` characterises any dump,
  `nx2/unpack.py` handles ZBIC NSOs, `nx2/image.py` lays the modules out, `nx2/insn.py` covers the Switch 2
  instruction forms. `docs/development.md` says the tools "were made to take any dump" and were checked against
  MKW's dump as a stand-in before Ocarina's existed. Nothing in this path is Ocarina-shaped.
- **The NVN2 layer is version-driven, not game-driven.** `host/nvn_native.c` implements the procs over guest memory;
  a proc that is missing is logged and answered (`unimplemented`, `host/nvn_native.c:2924`), and
  `HOS_NVN_STRICT=1` turns that into a stop. A new game's extra procs appear as a list in the log rather than as a
  crash. `nvn_native_proc` (`host/nvn_native.c:2944`) hands out a stub that "stops the game only when called".
- **The Vulkan replay follows the game.** `gpu/vk` replays the op stream the game recorded. Four viewports, four
  HUDs, split screen: all of that is draws and render targets the game submits, and the replay has no notion of a
  player count. This is why there is no "split-screen support" row in the table above — there is nothing to add.
- **Online was already thought about, for MKW.** The runtime does not pretend to be online: `ConnectToNamedPort`
  gives a session whose requests are answered empty (`host/hos_svc.c:472`), `nn::socket::detail::Poll` reports that
  nothing is ever ready (`host/nn_applet.c:204`), `nn::nsd::GetEnvironmentIdentifier` answers `"lp1"`
  (`host/nn_applet.c:227`), and `nn::account::EnsureNetworkServiceAccountIdTokenCacheAsync` returns a real failure
  Result rather than success (`host/nn_applet.c:221`). That last one is the precedent §4 follows: its comment says
  the generic stub's `0` "said the task started", and the game then waited forever on an `AsyncContext` nothing had
  set up. A failure was the fix.
- **Rumble is already mapped properly.** `rumble_motor` (`host/nn_hid.c:794`) converts HD-rumble amplitudes to
  motor strengths with Eden's curve, and `pads_poll` (`host/pad_sdl.c:71`) re-sends a held effect before it lapses.
  A racer leans on rumble and this part is in good shape.
- **Scripted input exists and is the only way anything gets tested.** `HOS_INPUT_SCRIPT`, `HOS_INPUT_LIVE` and
  `HOS_INPUT_FRAME_CLOCK` (`host/nn_hid.c:21`, `234`, `246`) drive the game from a test. Everything in §3 was built
  so that this still works, and so that it can reach a second pad.


## 3. Multi-player input: what was wrong, and what this branch does

### What was there

`host/nn_hid.c` served exactly one pad, and said so in its first line: "one Pro Controller (FullKey style) on Npad
No.1, from every XInput gamepad and the keyboard and mouse at once".

- `#define NPAD_NO1 0` (`host/nn_hid.c:40`) is the only npad id in the file.
- `npad_states` (`host/nn_hid.c:692`) computed `int connected = id == NPAD_NO1;` and wrote zeroed, unconnected
  states for every other id.
- `GetNpadStyleSet` (`host/nn_hid.c:738`) answered `STYLE_FULLKEY` for No.1 and `0` for everything else, so the
  game saw exactly one Pro Controller and no handheld.
- `read_input` (`host/nn_hid.c:611`) merged *everything* — every SDL controller, every XInput pad, the keyboard, the
  mouse and the script — into that one state. `pads_read` (`host/nn_hid.c:496`) ORs all XInput pads together, and
  `hos_sdl_pads` (`host/pad_sdl.c:162`) returns the merge of every open SDL controller, keeping the largest stick
  deflection (`host/pad_sdl.c:131`).
- Rumble went to every controller at once: `hos_sdl_rumble` (`host/pad_sdl.c:190`) sets one pair of globals that
  `pads_poll` sends to all open pads, even though the vibration handle already carries an npad id in its second
  byte (`host/nn_hid.c:757`, `779`).
- `ShowControllerSupport` (`host/nn_hid.c:750`) answered "one player, No.1".
- `SetSupportedNpadIdType` and `SetSupportedNpadStyleSet` (`host/nn_hid.c:745`, `746`) are empty, so the engine
  never recorded which ids and styles the game actually asked for.

For Ocarina that is correct and even useful: two pads on the desk both play, and the keyboard always works. For a
four-player game it is wrong in three separate ways — the other ids are not connected, their styles are empty, and
all four players' inputs are the same state.

### What this branch changed

`host/pad_sdl.c` already kept controllers in stable slots (`pads[MAX_PADS]`, `host/pad_sdl.c:27`; a disconnect
frees its slot, a new pad takes the lowest free one, `host/pad_sdl.c:77-103`), so the slot is a usable player
index. This branch keeps each slot's own buttons and sticks beside the merged state, and adds:

- `hos_sdl_pad_state(slot, buttons, stick)` — one controller's input, by position, and whether that slot is open.
- `hos_sdl_pad_count()` — how many controllers SDL has open (and starts SDL if nothing has yet).
- `hos_sdl_rumble_pad(slot, low, high)` — rumble for one controller; `hos_sdl_rumble` keeps its meaning (all of
  them) so the one-player path is untouched.

`host/nn_hid.c` grew a player layer: `npad_players()`, `npad_player()`, `npad_player_connected()`, `pads_mapped()`
and `pads_read_one()`. `read_input` and `pads_read` each took one more argument — the player, and the SDL slot to
read, where `-1` is "all of them merged", which is what one player gets. `npad_states` maps the id it is given to a
player, answers that player's connection state and reads that player's devices; `GetNpadStyleSet` answers
`STYLE_FULLKEY` for every connected player and an empty set for the rest; `ShowControllerSupport` answers with the
number of players whose pad is there; `vibration_send` routes by the npad id in the handle once more than one
player is served. `SetSupportedNpadIdType` and `SetSupportedNpadStyleSet`, which were empty, now log what the game
asked for — for a game nobody can boot, that one log line is the evidence of how many players it means to have.

**`HOS_NPAD_PLAYERS=<1..8|auto>` decides, and unset means one player.** Unset, empty, `0` or `1` give byte-for-byte
the old behaviour: only No.1 is connected, and it reads the merge of every device. `auto` serves one player per
attached SDL controller (at least one). A number serves that many regardless of what is plugged in, so a game can
be shown four pads whose players 2..4 are "disconnected" until they are.

Why not default to `auto`, when the task is a four-player game? Because the default has to be safe for the only
game anyone here can boot. Ocarina reads No.1 alone; under `auto` with two pads on the desk, pad 2 would stop
driving Ocarina, and there is no Kirby build to show that the multi-player path is right. A Kirby profile (§5) is
where `auto` belongs, next to the other per-game decisions. The switch obeys the repository's rule: new `HOS_`
switches are off and neutral by default.

Player 1 is always connected and always reads everything — keyboard, mouse, script, XInput and (when one player is
served) every controller. Players 2 and up are one SDL controller each, with no keyboard and no mouse: there is one
of each on a PC and no device identity to split them by. XInput is the same story — `XInputGetState` indexes its own
four slots with no relation to SDL's, so merging both per player would double-count a pad that both APIs see. Under
`auto` the per-player path is SDL only, and that is the honest answer; it is also no loss, because SDL sees every
pad XInput sees.

### Scripted input and a pad index

It fell out naturally, so it is there: a press may carry a `P<n>` token (`P1`..`P8`) anywhere in its button list,
and it then applies to that player only.

    HOS_INPUT_SCRIPT="50:A,52:P2+A,80:P2+LUP/4000"

Default is `P1`, so every existing script means exactly what it meant before, and `HOS_INPUT_FRAME_CLOCK` and
`HOS_INPUT_LIVE` are unchanged — the pad index lives in the press, not in the clock or the file format. This is not
a convenience: with no Kirby dump, a script is the *only* way the multi-player path can be exercised at all, and
`tests/test_multi_pad.py` uses it to do exactly that.

### How single-pad behaviour is known to be unchanged

- With `HOS_NPAD_PLAYERS` unset, `npad_players()` returns 1, so `read_input` takes `slot = -1` and reads through the
  original merged `pads_read(-1, ...)` path. Not one line of the per-player code runs: `pads_read_one` is not
  called, `hos_sdl_pad_state` is not called, and `npad_player()` maps only id 0.
- `tests/test_input_background.py` compiles the real `read_input`, `npad_states`, the player layer, the script
  parser and the live reader out of `host/nn_hid.c` and runs all 17 of its cases against them. Those cases pin the
  exact bytes of the npad state, the sampling numbers, the foreground and background rules, the exclusive interval
  and the frame clock, and they pass unchanged. Its two controller spies now assert the single-pad contract
  directly: `pads_read` fails the case unless production asked for the merge (`slot < 0`), and `hos_sdl_pad_state`
  fails it if production reads a per-player slot at all. So the old cases are provably still on the old path.
- The three ways this could have gone wrong were each tried, as mutations of the production source, to show the new
  check is not a guard that passes by checking nothing: making `read_input` always take the merged path, making
  `npad_player` map every id to player 1, and letting the keyboard reach every player. All three fail
  `tests/test_multi_pad.py`, at the case and line that should catch them.
- `tests/test_bound_keys.py`, `tests/test_alt_enter.py`, `tests/test_mouse_settings.py` and
  `tests/test_rumble_refresh.py` compile other slices of the same two files and are unchanged.
- `tests/test_multi_pad.py` (new) is the opposite check: with `HOS_NPAD_PLAYERS` set it asserts that No.2 is
  connected only while its controller is, that its style is reported, that its input is its own controller's and
  not the merge, that `P2` presses reach it and `P1` presses do not, and that rumble for npad id 1 goes to slot 1
  and not to slot 0.

### What is still missing in the input path

- **One binding table for four players.** `pad_bind[]` (`host/nn_hid.c:397`) and the Insert menu's Controls page
  are global. By-position mapping (`HOS_PAD_*`) means mixed controller types are fine, but a player who wants their
  own layout cannot have one. Half a day, and it is a menu problem more than an engine one.
- **Six-axis and touch are still single.** `GetSixAxisSensorHandles` hands out `0x100 + i` regardless of npad id
  (`host/nn_hid.c:759`) and the states are "at rest" (`:763`); the touch screen reports one untouched state
  (`:866`). Kirby does not obviously need either, and motion controls on a PC pad are a separate question.
- **No controller-support applet.** `ShowControllerSupport` answers immediately; it never shows the "press L+R"
  screen a four-player game uses to assign players. If Kirby waits on a real applet result this needs more than a
  count. Unknown until the game is run.
- **Disconnect is a no-op.** `DisconnectNpad` (`host/nn_hid.c:743`) does nothing. A game that drops a player on
  purpose will see the pad stay connected.
- **Styles are always FullKey.** A game that offers single-Joy-Con play asks for `JoyLeft`/`JoyRight` styles and
  splits one controller into two players. Not attempted: an SDL pad is not two Joy-Cons, and guessing how Kirby
  reads a sideways Joy-Con without the game is pointless.


## 4. `ldn`: the stance taken, and why

### What was there

`ldn` is **not implemented**, and that is not the same as absent. `nn::ldn` is listed in `OS_NAMESPACES`
(`nx2/standalone.py:27`), which means every `nn::ldn::` free function the game imports is replaced at the nn::
boundary by a generated stub (`gen_stubs`, `nx2/standalone.py:126`). That stub calls `a64_unimplemented`
(`third_party/a64-kit/rt/a64_rt.c:226`), which prints one line to stderr the first time —

    [a64] unimplemented import <symbol> (lr <address>)

— and then **sets `x0 = 0` and returns.** There is no other `ldn` code anywhere: `host/` has no `nn_ldn.c`, and
`grep -rn ldn` over `host/`, `gpu/` and `nx2/` finds that one line of `standalone.py` and nothing else.

`x0 = 0` is the problem. In the nn:: SDK, `0` is `ResultSuccess`. So the engine's answer to
`nn::ldn::Initialize()` is "local wireless started fine", to `nn::ldn::OpenAccessPoint()` "the access point is
open", and to a scan "the scan succeeded" — while writing nothing at all into the out-parameters the game passed,
which keep whatever was on the stack. That is the worst of the three possible answers: it is a lie, it is silent
after the first line per symbol, and the garbage it leaves behind is undiagnosable. The same mistake has already
been found and fixed once in this codebase, for a different service: the comment at `host/nn_applet.c:217` records
that the generic stub's `0` made Ocarina's network-account worker wait forever, and that returning a real failure
Result was the fix.

### The choice

**Loud refusal, not a working-looking stub. `ldn` stays unimplemented and now says so, every time, and never
reports success.** The implementation is `host/nn_ldn.c`, and `nx2/standalone.py` routes every `nn::ldn::` stub to
it instead of to `a64_unimplemented`.

- Default: the call is logged (the first calls per symbol in full, then periodically, so a per-frame caller cannot
  go quiet) with the symbol and its caller's `lr`, and `x0` is set to a **failure** Result in the `ldn` module
  rather than to success. Out-parameters are left alone, because the engine does not know what they are.
- `HOS_LDN_STRICT=1`: the first `ldn` call stops the run and names the symbol. This mirrors `HOS_NVN_STRICT`
  (`host/nvn_native.c:2925`), which exists for the same reason: sometimes you want the failure to be a wall.
- `HOS_LDN_SOFT=1`: the old `x0 = 0`. It exists only so that someone surveying a first Kirby boot can see how far
  the game gets, and the log says in so many words that every answer from then on is a fake.

The routing is name-driven from the symbols the dump actually has (`refusal_for`, which applies the same
free-function test `os_facing` does), not from a list of functions written out by hand, so it cannot silently miss
an `nn::ldn` import whatever that import turns out to be called. `tests/test_ldn_refused.py` runs the real
`gen_stubs` over an authored hook list and checks that every `nn::ldn` free function goes to the refusal while a
class member, another service and a namespace that merely starts with `ldn` keep the generic stub; then it compiles
the real `hos_ldn_refuse` and checks the three answers, including that a caller polling 3,000 times is logged more
than once and fewer than a dozen times. Dropping the routing, or making the refusal return `0`, each fail it.

The Result it returns is `203`, the `ldn` module with description 0. Module 203 is `ldn`'s by the public error-code
list (an `ldn` failure is reported as `2203-xxxx`). The description is deliberately left 0 rather than filled in
with something plausible: the descriptions the console returns for "no wireless" are **not known here** and are not
invented, and the only property the value needs is that it is not success.

Why a failure rather than a stub that fabricates an empty network: because a fabricated "no networks found, but
wireless is working" is exactly the "silently pretends a network exists" case, and it costs more than it buys. To
fake it convincingly the engine must know `ldn`'s state machine, its `NetworkInfo` layout, its event handles and
which call the game polls — and none of that can be known here without the dump, so every field would be invented.
A game that cannot start local wireless must already handle it, because wireless can be off or unavailable on real
hardware; a game that is handed a half-built network will go wrong somewhere further in, where the cause is gone.

Why not fatal by default: because the one game that can be tested is Ocarina, and Ocarina's import list cannot be
read on this machine (the lift's `gen/a64_stubs.c` is produced on the dump, and `work/` here is empty, so whether
Ocarina imports any `nn::ldn` symbol at all is **unknown**). A fatal default would risk breaking the only testable
game to make a point about a game that cannot be built. `HOS_LDN_STRICT=1` is there for whoever wants the wall.

The honest limit of the failure answer: `x0` is set from the symbol's *name*, which is all the engine has. For a
`Result`-returning function a non-zero `x0` is right. For a function that returns a state or a `bool`, non-zero is
wrong — and it is wrong towards "something is set" rather than towards "nothing is there", which is the direction
that could still mislead a game. Nothing can be done about that without the signatures, and the fix is not a
guess: the log names every `ldn` symbol the game calls, so the first real Kirby boot produces the exact list that
needs real answers. That list is the start of an `ldn` implementation, and this branch does not pretend to have
one.

### What a game would actually call

This is where the survey has to stop being specific, and it says so rather than inventing an API. The entry points
are not in this repository and cannot be read off Ocarina's dump: Ocarina has no local wireless, so its symbol
table has no reason to carry `nn::ldn` names. The way to get the real list, in order:

1. Lift the Kirby dump and read `gen/a64_stubs.c`: `gen_stubs` writes one stub per unimplemented OS-facing import,
   each with its demangled signature in a comment above it (`nx2/standalone.py:131`). Every `nn::ldn::` line in
   that file is a call the game makes, with its argument types.
2. Grep the lifted code for calls to those stubs to find the call sites, and run with the refusal above to see
   which ones are reached at boot as against only from the local-play menu.
3. Only then decide what each answer should be. The shape of the service (session, scan, access point, station,
   state events) is documented publicly, but which calls *this* game makes, in what order, and what it does with
   each result is a property of the game.

Scale, for honesty: implementing `ldn` so that two PCs running this engine could race each other is a project, not
a task — a new transport (UDP or similar) under a service that was designed for a radio, plus the game's own
session and packet layer on top of it, plus every timing assumption a radio protocol makes. Months. Making
*single-player* work costs nothing extra, which is what this section buys.


## 5. The game-specific layer: what a Kirby profile would have to supply

This is the real blocker, and this branch deliberately does not touch it. The code is installed unconditionally and
is written against Ocarina's addresses; another agent is building the general seam that would let a second game
plug in, in a different clone, so this section is a **requirements list for that seam**, not a design for one. Two
incompatible versions of the same refactor would be wasted work.

### What is hardcoded today

| Where | What it hardcodes | Line |
|---|---|---|
| `nx2/lift.py` | `oot_wide`, `oot_camera`, `oot_occlusion` installed for every `--standalone` lift | `158-164` |
| `nx2/lift.py` | `oot_patches.apply(gen)` on the emitted C, unconditionally | `183-184` |
| `nx2/oot_wide.py` | `MAIN = 0x80020000` and every widescreen, UI and movie site in Ocarina 1.1.1 | `15` and on |
| `nx2/oot_camera.py`, `nx2/oot_occlusion.py` | Ocarina's camera and occlusion sites | — |
| `nx2/oot_patches.py` | the dynamic-resolution branch, the text-speed function, the resolution-apply block, by address and by exact emitted C text | `25-37` |
| `host/nn_vi.c` | the graphics module's frame limiter at `mod + 0xfc`, nominal fps at `+0x2c8`, VRR flags at `+0x231`/`+0x232`, controller words at `+0x270 + 0x28/0x2c/0x58` | `296-333` |
| `host/hos_thread.c` | the graphics module is captured from `x19` when `GetSystemTick` is called with `lr == 0x8348c770` | `882` |
| `host/nn_vi.c` | the resolution object `hos_res_module` and its `+0xe20..+0xe5c` fields | `240`, `280-290` |
| `gpu/vk/vk_draw.cpp` | Ocarina's TAAU program keys `fd029a317a9a4b94` and `9dc98871eb7fc101` | `5411`, `5588`, `5766` |
| `gpu/vk/vk_shader.cpp` | the same keys, as the networks to keep | `1167` |
| `nx2/make_game.py` | `BUILD_ID = "e0f3ccfa"`: a build that does not start with it is refused | `38`, `247` |
| `host/hos_main.c` | `hos_game_is_mkw()` by build id — the precedent for gating per game | `68` |
| `host/nn_hid.c` | `hos_title_input` and `hos_frame_step_buttons` hooks, Ocarina's menus | `717-720`, `host/mkw_glue.c` |

### What a Kirby profile would have to answer

Every row is `None` here, with how it would be found. Not one of them is guessed at.

| Needed | Value | How it would be found |
|---|---|---|
| program id, update version, build ids | `None` | `nx2/dump.py --dump <folder> --set` on the Kirby dump; it already records all of these |
| `main`'s base in the image | `None` | `nx2/image.py`'s layout for that dump (Ocarina's is `0x80020000`) |
| whether the widescreen/camera/occlusion patches apply at all | `None` | they are Ocarina sites; Kirby needs its own, found the way Ocarina's were (NX2navi's patch list, or by hand on the lifted code) |
| graphics-module offsets (frame limiter, nominal fps, VRR gate) | `None` | the same way Ocarina's were: find the module's frame-end clock read, log the object, and read the fields that change with the frame rate. `HOS_FPS_LOG`, `HOS_RES_LOG` and `HOS_TICK_LOG` already exist for this |
| the `lr` that identifies the graphics module's clock read | `None` | `HOS_TICK_LOG=1` lists the callers of `GetSystemTick` by address; the frame-end one is the one that fires once a frame |
| the upscaler / TAAU program keys | `None` | `nx2/shader_census.py` over Kirby's `Nin_Ounce_NVN2` packages, plus `RX_VK_FRAMELOG` to see which program runs at the end of a frame |
| the supported build id | `None` | the `main` build id from `nx2/dump.py`'s table |
| whether a frame pacer needs hooking | `None` | MKW's is gated on its build id (`host/hos_main.c:68`); Kirby's would be found by profiling, "never by address coincidence" (`docs/design.md` §4) |

What the seam itself needs, stated as requirements:

1. **A per-game module, chosen by build id, not by a flag.** `hos_game_is_mkw()` is the existing shape: identify
   the game from `hos_main_build_id`, which `nx2/standalone.py:write_image_c` already emits.
2. **Lift-time patch installation must be a list the profile supplies**, not three `import oot_*` lines. A profile
   with no patches must lift cleanly — that is the state a new game starts in.
3. **Every guest offset in `host/` must come from the profile**, and a profile that does not know an offset must be
   able to say `None` and have the feature turn itself off rather than read address 0xfc of nothing. `nn_vi.c`
   already tolerates `hos_gfx_module == 0` (`:294`); the offsets below it do not have the same escape.
4. **The renderer's program keys must be data**, not string literals: `RX_VK_UPSCALE_KEYS` and `RX_VK_TAAU_KEYS`
   already accept them from the environment (`gpu/vk/vk_draw.cpp:5588`, `5766`), so the profile needs somewhere to
   put them, not a new mechanism.
5. **`HOS_NPAD_PLAYERS` belongs in the profile.** This is the one thing §3 asks of the seam: Kirby's profile would
   set `auto`, Ocarina's would leave it at one, and neither would need an environment variable.


### What was built (2026-11-13)

The seam is `nx2/game_profile.py`, in the shape Lakitu (the Mario Kart World fork, `fork/mario-kart-world` at
`8c47a50`) gave it: the same file, the same `Profile`, `Gfx` and `Upscale` fields, the same committed C view
`game/game_profile.h` with the same `GAME_*` macros, and the same rule that an unknown value is `None` and turns its
feature off. A profile written for one fork reads the same in the other. Where Dragoon's requirements above went
further, it adds to that shape rather than changing it:

| Requirement | How it is met |
|---|---|
| 1. chosen by build id, not a flag | `for_build()` matches main's build id; `HOOKSHOT_GAME=<key>` still overrides. A build no profile knows gets `none`: nothing patched, nothing hooked. Lakitu picks by `HOOKSHOT_GAME` alone. |
| 2. lift patches are a list | `nx2/lift.py` installs `profile.patch_modules` and passes `profile.code_patch_module` to `chunks.edit_c`; it names no game. Ocarina's modules are unchanged (they already check each site's instruction word), so upstream's fixes to them still apply. |
| 3. every guest offset from the profile, `None` turns it off | `host/nn_vi.c` reads the graphics module's offsets from `game_profile.h`. The module is only captured at the profile's return address (`Hooks.gfx_clock`, Ocarina's `0x8348c770`), and only when the running image's build id is the profile's (`hos_game_profile_live()`), so on any other game nothing is written. `host/game_profile_check.h` stops a build whose profile names the module but not its offsets. The present-poll experiments' return addresses go the same way. |
| 4. the renderer's program keys are data | The renderer is built once for every game (the AppImage's is prebuilt), so it keeps `game/game_profile.h`'s keys as defaults and the runtime, compiled for its game, hands it its own through `HOOKSHOT_PROFILE_*` at start. `RX_VK_TAAU_KEYS` and `RX_VK_NETWORK_KEEP` still stand over both. |
| 5. `HOS_NPAD_PLAYERS` in the profile | `npad_players`: Ocarina 1, Kirby `auto`. The variable still overrides. |
| the build-id gate | `make_game.py` accepts every build a profile knows and names the game in its check. |

A standalone lift writes the dump's own profile to `<work>/sa/game/game_profile.h`, and `nx2/build.py` puts that
folder ahead of `game/` for the runtime. `tests/test_game_profile.py` checks the choice by build id, that the Kirby
and empty profiles patch nothing, that Ocarina's modules leave out every site whose word differs, that no Ocarina
offset, return address, program key or build id is left as a literal outside the profile, that the committed header
is what the profile emits, and that the C guard rejects a half-filled profile.

The Kirby profile (`kar`) holds only what was measured on the dump: program id, version, main's build id and main's
base (§8). Everything else is empty, with a comment saying how it would be found.

Not moved, on purpose: Ocarina's host features (`host/oot_*.c`, the title and frame-step hooks) are reached only from
Ocarina's code patches, which a game without them never installs, so they need no gate of their own.

## 5a. The rename, and what was deliberately not renamed

The product is **Dragoon** now: the window and its class, the console title and the graphics-card warning
(`host/hos.h`'s `HOS_GAME_TITLE`, `host/display.c`, `host/posix/display_sdl.c`, `gpu/vk/vk_main.cpp`), the builder on
both platforms, its desktop entry and shortcut, the AppImage (`Dragoon-<version>-x86_64.AppImage`), the CI artifact
names, `readme.md`, `CONTRIBUTING.md` and `docs/versioning.md`.

What was left alone, on purpose:

- **File names, switches and identifiers**: `hookshot.exe` / `hookshot_vk.so`, `hookshot_options.ini`,
  `hookshot_env.txt`, `hookshot_log.txt`, the `HOOKSHOT_*` and `HOS_*` environment variables, `HookshotBuilder` as a
  namespace and assembly, `io.github.NotRexed.Hookshot` as the Wayland app id. These are a file format and a
  command-line interface, not a product name.
- **`(hookshot)` comment markers and `namespace mkwvk`.** They mark which lineage a change came from, and keeping
  the paths and markers identical to mkw-recomp's is what lets its fixes still apply here (`docs/design.md`
  section 2). Renaming them would make every future `git apply -3` of an mkw-recomp patch conflict.
- **URLs and references to upstream issues.** `github.com/NotRexed/Hookshot` and "Hookshot issue 14" are references
  to a real project; rewriting them would turn them into links and citations that go nowhere. This fork has no
  repository of its own, so it has no URL to put there.
- **`docs/design.md` and `docs/development.md`.** They describe the Ocarina port in detail, down to its dump's build
  ids and its open bugs. They now carry a banner saying so and keep their own name, because renaming the title of a
  document whose every statement is about Hookshot and Ocarina would make it lie. The old product readme was moved
  whole to `docs/hookshot-readme.md`, with a banner of its own: its engine, build and troubleshooting instructions
  still apply, and everything it says about status and releases is Hookshot's.

## 6. Everything else between this and booting Kirby Air Riders

Honest list, in the order it would be hit.

1. **No dump.** As written: nothing can be lifted, built or booted. There was one after all; §8 is the run.
2. **Stage S0-S1 on the real dump**: the six Switch-2 signals, then full instruction coverage. MKW and Ocarina each
   needed new instruction forms (UDOT for Ocarina, `nx2/insn.py`); Kirby will need whatever it uses that neither
   did, and that is a day or two of `nx2/test_insn.py` against Unicorn — a well-worn path, but not a free one.
3. **The SDK version.** Ocarina is SDK/NVN2 22.2.8, MKW 21.4.0 (`docs/design.md` §5). Kirby ships later than both.
   (§8: Kirby's SDK is in fact older, 20.5.26. The same kind of work applies, from the other side.)
   A newer SDK means more NVN2 procs, possibly new `nn::os` overloads reaching kernel calls the runtime lacks —
   which is exactly the list Ocarina's S3 had to work through. Expect a week of boot work, with the log naming each
   missing piece as it is hit.
4. **The game-specific profile of §5.** Without it the game may well boot, since every one of those patches is an
   improvement rather than a requirement — but the frame limiter, dynamic resolution, widescreen and the upscaler
   stand-in will all be inert or wrong, and the build-id gate in `make_game.py` refuses outright until it is
   changed.
5. **Four player profiles.** `nn::account` was one hardcoded user. Kirby's main imports `ShowUserSelector`,
   `OpenUser`, `PushOpenUsers`, `PopOpenUsers` and `LoadProfileImage`, and not `ListAllUsers`: it has players pick
   accounts. Done: `HOS_ACCOUNT_USERS=<1..8>`, unset the profile's count (Ocarina 1, Kirby 4). User 1 keeps the uid it
   always had, so its save folder is the same; users 2 and up are "Player 2" and on, each with its own save folder.
   `ShowUserSelector` has no picker to show, so it hands out the first user not open and answers a failure once all
   are; it logs the settings it was given, whose layout is not known here. `LoadProfileImage` and the token cache
   answer failures rather than invented data. `GetUserId` now answers the handle's own user, not user 1.
   `tests/test_account_users.py`.
6. **`ldn`.** Single-player boots under §4's refusal. Local four-player over real wireless is a project and is not
   attempted here. Four players on **one** PC with four pads is what §3 provides and needs no `ldn` at all —
   whether Kirby's own four-player mode is written as local-wireless-only or as same-console split screen is the
   single most important unknown in this document, and it is unknowable without the game.
7. **Online play.** Out of scope, and the runtime is deliberately offline (§2).
8. **Performance.** Ocarina is CPU-bound at 30-36 fps in gameplay on a Ryzen 5 7600X (`docs/design.md` §9, §10). A
   racer at 60 fps with four viewports is a harder target, and §4's lesson applies: measure, do not assume
   (`docs/design.md` §4 rule 7, and the open "dark frames" and device-loss items in §11).
9. **The open bugs Ocarina still has** — intermittent device loss, dark frames, six programs that do not lower —
   come along with the fork, since they are in the shared renderer.

What this branch is, then: the input path a four-player game needs, an `ldn` answer that does not lie, and a
written-down list of what the game-specific layer would have to supply. It cannot and does not claim anything about
Kirby Air Riders itself.


## 7. Outstanding on this branch

- **Hookshot v2.3.0-rc.1 is merged** (`c78af5fc`, 1,483 commits past the old base `v2.3.1-5`, among them the 126
  taken from the mirror of the gone NotRexed/Hookshot). 22 files conflicted. The input path was the substantial one:
  upstream added HD rumble, motion, Steam stand-in pads, paddles, the touchpad click, a stick dead zone, the
  ocarina's note layer and the control bridge exactly where the per-player reads live. The player layer is kept;
  the bridge, the notes, the host's taps and the keyboard and mouse stay with player 1; HD rumble goes per pad as
  motor rumble already did. The rest were the product name meeting upstream's new strings. The merge commit says
  what each took.
- **Checked here**: `tests/run_all.py` on Linux with the repository's toolchain, at each commit; the renderer built
  alone (`RX_VK_ONLY=1`) with the profile header; the host files that read the profile compiled against Ocarina's,
  Kirby's and the empty profile's headers.
- **Not checked here**: a full Ocarina build and run. There is an Ocarina dump on the second drive too (§8), so this
  is now possible; it was not done in this pass, and it is the next thing that would prove the merge and the seam on
  the game that already ran.
- **`HOS_NPAD_PLAYERS` and `HOS_ACCOUNT_USERS` default from the profile**: Ocarina 1 and 1, Kirby `auto` and 4.

## 8. The dump, and the first lift (2026-11-13)

The survey searched `/` and found no dump. The second NVMe drive holds the Switch 2 `.nup` files the Eglantine
emulator plays (its `romsPath`), among them `Kirby Air Riders [040077201BE50000][v1.3.3].nup` (41.8 GB) and Ocarina's.
Everything below was read from that file in place; the extracted modules, the romfs, the lift and the build are in the
gitignored `work/kar/`, and none of it is in the repository.

### What the dump is (`nx2/nup.py`, `nx2/dump.py --no-romfs`)

- Two titles: the base (v0) and the update, v458752, which is what was lifted. Romfs 21.3 GB, 621 files.
- **Switch 2 by all six signals.** Program id `040077201BE50000`. 39-bit address space, main thread priority 44,
  core 0, 1 MB stack, system resource 32 MB.
- **SDK and NVN 20.5.26.** Older than Ocarina's 22.2.8 and MKW's 21.4.0, not newer as §6 expected.
- Modules (rtld's load order):

  | module | build id | .text | .rodata | .data | .bss |
  |---|---|---|---|---|---|
  | rtld | 184b92f6e9da4716d3110a8b6295a067197855de | 9,856 | 2,692 | 49,160 | 65,528 |
  | main | 9ede03836568bcd34434dfc29bc61e0451dbbe3e | 96,143,504 | 46,653,588 | 10,124,984 | 1,999,176 |
  | subsdk0 | 11a5672eeabf787e8e510902825606d41714645e | 2,332,800 | 17,984,028 | 739,840 | 108,032 |
  | subsdk1 | 3cd75f1ea17d5e590b8f4326a521849f2dd2bc45 | 937,920 | 174,076 | 55,049,304 | 132,008 |
  | sdk | 89ea3537110354e8427004a0266744c8ffe506dc | 8,092,176 | 6,227,604 | 391,280 | 1,996,688 |

  main's code is half again Ocarina's (63 MB). subsdk1 is NVIDIA's DLSS library for NVN (`NVNdlssLibrary`, its 55 MB
  of data the networks). main names 733 NVN procs, 66 of them DLSS or mesh-task ones, and carries middleware for
  PhysX 5.2, `SMAA_For_NDSR`, the movie player and NPLN (online: gRPC, WebRTC, ngtcp2).
- The services it may open include `ldn:u` (local wireless), `irs`, `nfc`/`nfp` (amiibo), `friend`, `bcat`, `nifm`,
  `bsd`/`ssl` and `caps` (screenshots).

### The lift (`nx2/lift.py`)

- **The loader stopped first**: `subsdk1: relocation type 1031 at 0x1197b0 not supported`. 1031 is
  R_AARCH64_TLSDESC, a thread-local variable reached through a descriptor; the whole game has one, in the DLSS library,
  with no symbol. a64-kit's loader takes none. `nx2/image.py` now binds each descriptor to a host stop
  (`hos_tlsdesc_unsupported`, which names it and ends the run if the game ever calls it): the TLS itself is still not
  implemented.
- Past that, **100.0000 % of 26,456,886 instructions translate** in 278,254 functions (standalone: 279,081 in the
  plain lift), with 11,143 jump tables and one `br` without a table (`0x84a04b68`, x9). No new instruction form was
  needed, unlike Ocarina's UDOT. 13,440 sites reach undefined encodings (traps, as on the hardware).
- The standalone lift took 11.5 minutes and 7 GB and wrote 411 files, 2.0 GB of C. The `kar` profile was chosen by
  main's build id; it installs no patches and no code patches, and its header went beside the C.
- 535 functions are answered by `host/`; **292 OS-facing imports are stubbed**, by namespace: `codec` 59 (Opus),
  `socket` 47, `audio` 33, `nfp` 30 (amiibo), `album` 23 (screenshots), `hid` 22, `ldn` 17, `nifm` 9, `ro` 9,
  `swkbd` 8, `oe` 6, `bcat` 6, `friends` 5, and a few each of `vi`, `time`, `ssl`, `pctl`, `movie`, `chat`, `pl`.

What those say:

- **`ldn` (§4's open question, answered):** 17 functions, all routed to the refusal: `Initialize`, `Finalize`,
  `GetState`, `Scan`, `Connect`, `Disconnect`, `CreateNetwork`, `DestroyNetwork`, `OpenAccessPoint`,
  `CloseAccessPoint`, `OpenStation`, `CloseStation`, `GetNetworkInfo` (two forms), `SetAdvertiseData`,
  `SetProtocol`, `SetStationAcceptPolicy`. That is a whole local-play implementation; whether single-console
  four-player avoids it is still for a run to say.
- **`hid`:** the game reads its pads with `GetNpadState` (one state), not `GetNpadStates`. That went to the generic
  stub, which writes nothing: no input at all. **Fixed** (`3b6d6b81`): the single form for FullKey, Handheld, JoyDual,
  JoyLeft and JoyRight. Still stubbed: the GameCube controller's state (no GameCube style is ever reported),
  `GetNpadJoyHoldType`, the Joy-Con assignment modes, `GetPlayerLedPattern`, vibration files and devices, six-axis
  rest and stop, the touch screen's init, and `SetExplainText` for the controller-support applet.
- **`account`:** see §6, item 5; the user selector and the open-user stack are answered now.

### The build and the boot

**Built**: `nx2/build.py --standalone --jobs 16` compiled 831 files in 23 minutes, 0 failed; `hookshot` is 569 MB,
`hookshot_vk.so` 19 MB. The first link found the five `GetNpadState` stubs of a lift made before that fix beside the
host's own; a fresh lift (287 stubs) linked.

**Booted**, headless and muted (`HOS_HEADLESS=1 HOS_AUDIO=0`, 120 s limit, `hookshot image.bin --romfs ... --user ...`).
Each wall was hit, fixed where its meaning was plain, and the boot run again:

1. *Run 1.* Runtime start, the renderer on the RTX 5060 Ti, libc's and the SDK's init, `nninitStartup`, main's
   `DT_INIT`, then **an abort**: main's static initialisers build a `std::random_device`, whose `getentropy` calls
   `nn::crypto::GenerateCryptographicallyRandomBytes`, which asks `csrng` through `sm:`, which this runtime answers
   empty. **Fixed** (`host/nn_crypto.c`: the system's random bytes).
2. *Run 2.* Past that: every module's `DT_INIT`, **`nnMain`**, the romfs mounted, eight fibers made, PhysX's job
   workers running the game. hid starts: the game supports **styles 0x1f** (FullKey, Handheld, JoyDual, JoyLeft,
   JoyRight) and **Npad No.1 to No.8 plus handheld** (`SetSupportedNpadIdType`; §3 wanted this line, and it says the
   game means to have up to eight players). The NVN device is made (427 procs), the window, `BootGraphicsSystem`.
   Then **a crash** in `phx.JobWorker 4`, reading guest `0x3feb3100500` (above the 39-bit space, so not a pointer the
   game made in its own memory). `nn::oe::GetDefaultDisplayResolution` and its change event had gone to the generic
   stub just before, leaving the size as stack garbage, so they were answered (the runtime's output size; an event
   never signalled).
3. *Run 3.* **The same crash at the same address and the same guest stack**: deterministic, and not those two calls.
   main's symbol table is stripped, so the stack (`0x84c61fd4`, `0x84c5ff00`, `0x8102a074`, `0x84f5b1f8`, ...,
   from `nnMain` at `0x85bc8f4c`) has no names. Finding which load builds that address, from which value, is the next
   piece of work; it was not guessed at.

Saves: these runs passed `--user work/kar/user` and never reached a save mount, so nothing was written outside
`work/`. Started without `--user`, the runtime used to put every game's saves in `$XDG_DATA_HOME/hookshot`, Ocarina's
folder; Kirby's is now `hookshot-kar` (`GAME_SAVES_DIR`, below), and a run should still pass `--user` or its own
`XDG_DATA_HOME` until it has been rebuilt with that.

Still answered by the generic stub on the way (0 and nothing written): `nn::ro::ProtectRelro` and `Initialize`,
`nn::oe::GetPseudoDeviceId`, `nn::hid::InitializeTouchScreen`. One line is Ocarina's and says nothing about Kirby:
"the depth mode is not the one the prepared shaders were made for" refers to Ocarina's prepared shaders.

### What blocks a Kirby boot now, in order

1. **The job-worker crash above** (guest `0x3feb3100500` read from lifted code under `0x84c61fd4`). Unknown cause.
   Candidates to rule out first: a value an unanswered or half-answered call left behind (the remaining stubs above,
   or an NVN proc of SDK 20.5.26 answered by the layer written for 22.2.8 and 21.4.0), and a fiber/TLS assumption
   (the TLS descriptor's stop did not fire, so it is not that one).
2. **SDK 20.5.26's names.** 15 of the runtime's handlers name functions this SDK does not have (`nn::os::SystemEvent`
   constructors, `nn::codec` video decoders, two `nn::audio` calls). Where Kirby calls this SDK's own form of one of
   them, that call runs the SDK's code, which talks IPC and gets empty answers; each one reached will show as the
   `sm:` abort run 1 showed.
3. **The TLS descriptor** in the DLSS library, if the game reaches it (it stops the run and says so).
4. **The game profile's unknowns**: the graphics module's clock return address and offsets, the upscaler keys, any
   widescreen or camera patches. None blocks a boot; each is a feature that stays off until found, by the methods in
   the profile's comments.
5. **Local play**: 17 `nn::ldn` calls refused (§4), and 47 `nn::socket` and 9 `nn::nifm` ones stubbed, which keeps
   the game offline.
