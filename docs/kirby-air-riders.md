# Kirby Air Riders on this engine: what is here, what is missing

**Dragoon** is this fork's name, as Hookshot is the Ocarina port's and BlueShell is mkw-recomp's.

This is a survey, written before any code in this branch changed. It asks one question: if a *Kirby Air Riders*
(Switch 2) dump were put in front of this engine, what would it already do, and what would have to be built?

**Nothing here was run against Kirby Air Riders.** There is no Kirby dump on this machine; the only dump is Ocarina
of Time's, and game data never enters this repository. Every statement below is about code in this repository, with
the file and line it comes from. Where a value would have to come from the game, it says so and says how it would be
found — it is never guessed at. The evidence that this branch is sound is that Ocarina still builds and its checks
still pass.

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
| Game-specific layer (patches, graphics-module offsets, upscaler key) | **hardcoded to Ocarina** | §5: a profile, being built elsewhere |
| Four player profiles (`nn::account`) | one user, hardcoded | a day, §6 |
| Split-screen / four viewports in the renderer | nothing to do: the replay follows the game | — |
| Build gate on Ocarina's build id | hardcoded, `nx2/make_game.py:38` | an hour, §5 |

The short version: **the engine is not the problem, the game-specific layer is.** Four-player input was a day's
work and is in this branch. `ldn` is a project and is not attempted. Everything that actually blocks a Kirby boot is
the per-game profile of §5, plus the unknowns that can only be found by lifting the dump.


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

`host/nn_hid.c` grew a player layer: `npad_players()`, `npad_player()`, `npad_player_connected()` and
`pads_read_one()`. `npad_states` now maps the id it is given to a player, answers that player's connection state,
and reads that player's devices; `GetNpadStyleSet` answers `STYLE_FULLKEY` for every connected player;
`ShowControllerSupport` answers with the number of players served; `vibration_send` routes by the npad id in the
handle when more than one player is served.

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

- With `HOS_NPAD_PLAYERS` unset, `npad_players()` returns 1, and `npad_states` calls the original `read_input`
  through the original merged `pads_read(-1, ...)` path. The per-player code is not reached.
- `tests/test_input_background.py` compiles the real `read_input`, `npad_states`, the script parser and the live
  reader out of `host/nn_hid.c` and runs all 17 of its cases against them. Those cases pin the exact bytes of the
  npad state, the sampling numbers, the foreground and background rules, the exclusive interval and the frame
  clock. They pass unchanged, and `read_input_player` is given a fixture body that fails the test if it is ever
  called — so the old cases are provably still going through the old path, not through a stand-in.
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


## 6. Everything else between this and booting Kirby Air Riders

Honest list, in the order it would be hit.

1. **No dump.** Nothing can be lifted, built or booted. Everything below is reasoning from code, not from a run.
2. **Stage S0-S1 on the real dump**: the six Switch-2 signals, then full instruction coverage. MKW and Ocarina each
   needed new instruction forms (UDOT for Ocarina, `nx2/insn.py`); Kirby will need whatever it uses that neither
   did, and that is a day or two of `nx2/test_insn.py` against Unicorn — a well-worn path, but not a free one.
3. **The SDK version.** Ocarina is SDK/NVN2 22.2.8, MKW 21.4.0 (`docs/design.md` §5). Kirby ships later than both.
   A newer SDK means more NVN2 procs, possibly new `nn::os` overloads reaching kernel calls the runtime lacks —
   which is exactly the list Ocarina's S3 had to work through. Expect a week of boot work, with the log naming each
   missing piece as it is hit.
4. **The game-specific profile of §5.** Without it the game may well boot, since every one of those patches is an
   improvement rather than a requirement — but the frame limiter, dynamic resolution, widescreen and the upscaler
   stand-in will all be inert or wrong, and the build-id gate in `make_game.py` refuses outright until it is
   changed.
5. **Four player profiles.** `nn::account` is one hardcoded user (`host/nn_applet.c:153`, `322`). A four-player
   game may ask for a user per player, or may treat players 2-4 as guests. A day, once it is known which.
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
