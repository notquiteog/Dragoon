<h1 align="center">
  <br>
  <b>Dragoon</b>
  <br>
</h1>

<h4 align="center">Kirby Air Riders (Nintendo Switch 2) on a PC with no emulator — a fork of Hookshot, at the start of
its work.</h4>

<p align="center">
  <a href="#what-this-is">What this is</a> |
  <a href="#what-works">What works</a> |
  <a href="#the-survey">The survey</a> |
  <a href="#build-and-test">Build and test</a> |
  <a href="#what-the-names-mean">What the names mean</a> |
  <a href="#credits">Credits</a> |
  <a href="#license">License</a>
</p>

## What this is

**Dragoon** is a fork of [Hookshot](docs/hookshot-readme.md) retargeted at *Kirby Air Riders*. Hookshot is a static
recompilation of the Switch 2 *Ocarina of Time* remake: the game's ARM64 code is translated to C and compiled into a
native program, Horizon OS and NVN2 are reimplemented on the host, and the GPU work is replayed on Vulkan 1.3. None of
that is emulation, and all of it happens on your own PC from your own copy of the game. Hookshot itself came from
mkw-recomp (Mario Kart World), so this is the third game the same pipeline has been pointed at.

The names: Hookshot is the Ocarina port's, BlueShell is mkw-recomp's, and Dragoon is this one's.

## What works

**Nothing plays yet, and this fork cannot be made to play anything.** There is no Kirby Air Riders dump on the machine
this was written on, so not one line of the game has been lifted, built or booted here. Anything that claimed
otherwise would be invented.

What this fork has actually done, all of it checked against *Ocarina*, which is the only game that can be built and
run here:

| | State |
|---|---|
| A written survey of what the engine already gives Kirby and what it does not | [docs/kirby-air-riders.md](docs/kirby-air-riders.md) |
| Four local players: Npad No.1 to No.8, their styles, their connection state, one controller each | done, `HOS_NPAD_PLAYERS` |
| Per-player rumble, routed by the Npad id the vibration handle carries | done |
| A pad index in the scripted test input (`P2` in a press) | done |
| `ldn` (local wireless) answered honestly instead of being reported as working | done, see below |
| The game-specific layer (patches, graphics-module offsets, upscaler keys) | still Ocarina's; the survey says what a Kirby profile would have to supply |
| Everything else | inherited from Hookshot unchanged |

### Four players

`HOS_NPAD_PLAYERS=<1..8|auto>` says how many players to serve. Unset — the default — is one player, and then the input
path behaves exactly as Hookshot's does: Npad No.1 alone, reading every controller, the keyboard and the mouse merged
together. `auto` serves one player per attached controller. The default is deliberately the old behaviour: Ocarina is
the only game anyone can test here, and nothing could show that the multi-player path is right instead.

### `ldn`

Local wireless is **not implemented**, and now says so. Before this fork, every `nn::ldn` call was answered `0` by the
generic import stub, and `0` is `ResultSuccess` in the nn:: SDK — so the engine claimed local wireless had started,
that an access point was open, that a scan had succeeded, while writing nothing into the game's out-parameters. Now
each call is logged with its symbol and answered with a failure, so a game falls back to single player, and nobody is
told a network exists that does not. `HOS_LDN_STRICT=1` stops the run at the first call instead; `HOS_LDN_SOFT=1`
restores the old `0` for whoever is surveying a first boot, and says in the log that every answer is then a fake.
Online play is out of scope. [docs/kirby-air-riders.md](docs/kirby-air-riders.md) section 4 has the reasoning.

## The survey

[docs/kirby-air-riders.md](docs/kirby-air-riders.md) is the main thing in this fork. It goes through what the engine
already provides that a four-player Switch 2 racer needs, what is missing and what each missing piece would cost, with
the file and line behind every claim. Every value that would have to come from the game itself is written as `None`,
with how it would be found.

## Build and test

As Hookshot: see [docs/development.md](docs/development.md) and [docs/hookshot-readme.md](docs/hookshot-readme.md).
The renderer alone, and the whole game-free check:

```
EMSDK=<emsdk> RX_CPM=<cpm cache> OOT_WORK=<work folder> RX_VK_ONLY=1 python3 -I nx2/build.py --standalone --jobs 6
python3 -I tests/run_all.py
```

The input path's own checks are `tests/test_input_background.py` (one player, the path Ocarina reads),
`tests/test_multi_pad.py` (more than one) and `tests/test_ldn_refused.py`.

## What the names mean

Only the product's name changed. Inside the tree, `hookshot` is still the name of the program, the options file, the
log, the environment-file and the `HOOKSHOT_*` switches, and `(hookshot)` still marks a change this lineage made on top
of mkw-recomp. That is on purpose twice over: those names are a file format and a command-line interface that
people's existing folders depend on, and keeping the sources' paths and markers identical to mkw-recomp's is what lets
its fixes still apply here (`docs/design.md` section 2). Renaming them would cost a day and buy nothing.

`namespace mkwvk` in the renderer is mkw-recomp's and is left alone for the same reason.

## Credits

Everything this is built on is Hookshot's and mkw-recomp's: see [docs/hookshot-readme.md](docs/hookshot-readme.md) for
the full list — NotRexed's a64-kit, suyu's AArch64 translator, Eden's shader recompiler, TOTKPort, mkw-recomp,
Eglantine, and the people who found the bugs.

## License

GPL-3.0-or-later, as Hookshot and mkw-recomp: a64-kit's translator is suyu's (GPL-2.0-or-later) and Eden's code is
GPL-3.0. No game files, keys, dumps or anything made from them are in this repository, and none ever will be.
