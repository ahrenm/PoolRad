# PoolRad

A C++20 reconstruction of the DOS **Pool of Radiance**, using the original game
resources and SDL3 for graphics, input and audio. The runtime is organised as
**GBEngine**: an asset decoder, ECL script interpreter, party/rules services,
exploration and tactical combat, with native host services behind platform interfaces.
It runs the recovered game logic directly; it does not run the DOS executable in
an emulator.

**Status: 0.1 beta preparation.** A human-led campaign playthrough has reached the
ending and returned to Phlan. Some conditional rules and original-runtime
comparisons remain open; this is not a claim of complete DOS parity.
See [development gaps](docs/OpenDevWork.md) and [open QoL work](docs/OpenQOLWork.md).

**Thar will be bugs!!!**

## What this IS/ISN'T & where it came from

This project is a reimplementation of the orignal game which utilises the originals assets. 

It is NOT a 1 for 1 port of the program or executable. Each aspect has been iteratively built/tested from a variety
of sources and as such may have subtle differences/bugs compared to the original. 

Sources/credits include:
- Yes, an REA decompile of the DOS x86 machine code, largly used as a behavioural guide
- Memory dumps of the DOS game to trace some calculations and behaviours
- Posts and info garned from the [FRUA Community Fourms](https://forums.goldbox.games/index.php?topic=2.0)
- Bil Simser's seminal [Gold-Box-Explorer](https://github.com/bsimser/Gold-Box-Explorer)
- Simon Pilgrim's magnum opus, the hand crafted RE of [Curse of the Azure Bonds](https://github.com/simeonpilgrim/coab)
- Your's truely's love and obsession with this game over the past ~38 years.

## SSI Shout out

The more you dive into it, the more the GB engine impresses. The ECL scripting is kinda mind blowing for 1987
and hasn't really had it's moment in the spotlight. There is a reason why nearly 4 decades later,
people like me want to pull these games apart and are still finding hidden secrets. 

...yeah, you me heard me right. PoolRad hasn't given up everything she's got to give just yet..... ;-)

Full recognition and mad respect to the boys and girls at SSI & SSI Special Projects.

## What you need (tldr: the GOG version of the game)

- A supported copy of the original DOS game resources. The Windows setup screen
  can locate a GOG installation or accept a pasted POOLRAD folder path.
- The GOG **Adventurers Journal.pdf** for optional in-game Journal images.
- A writable folder for the executable, generated content and saves.


Original DOS executables, archives, PDFs, extracted text/images and saves are
**not included** in this source repository. Setup authenticates the supported
START.EXE/GAME.OVR profiles and the PDF before fixed-offset extraction. It copies
117 required game resource files and omits bundled DOS saves and characters.


## Build

The current release target is **Windows x64**. The validated release toolchain is
MinGW GCC 16.1 with SDL3 headers/runtime 3.5.0. Other platforms and a fully linked
MSVC application still need validation.

Requirements: **CMake 3.25+**, a **C++20 compiler**, and a compiler-matched **SDL3
shared development SDK**. The toml++ 3.4.0 header and its license are vendored.
No PDF library or OCR dependency is required; image extraction uses Windows WIC.

From the repository root, with SDL installed as a CMake package:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/SDK/SDL3"
cmake --build build --config Release --parallel
cmake --install build --config Release --prefix "$PWD/dist/PoolRad"
```

For a MinGW SDL SDK containing `include`, `lib` and `bin` but no CMake package:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DPOOLRAD_SDL3_ROOT="C:/SDK/SDL3"
cmake --build build --parallel
cmake --install build --prefix "$PWD/dist/PoolRad"
```

Put the chosen compiler and build tool on PATH. Use a new build directory when
switching compilers. SDL's [CMake guide](https://wiki.libsdl.org/SDL3/README-cmake)
explains how to build/install its SDK and select the shared-library package.
Release is the default for single-configuration generators; GNU/Clang releases
use the validated `-O2 -DNDEBUG` defaults. Warnings are treated as errors.

The install step stages the executable, SDL/compiler runtime DLLs, notices and
this README. On Windows it also creates **Pool of Radiance QOL**, a shortcut
which launches `poolrad.exe --qol`. PowerShell is required for that shortcut.
MSVC runtime redistribution is not handled by the MinGW DLL-copy rule.

CMake builds the current application and its four runtime libraries. Historical
phase drivers, proprietary corpus fixtures and workspace research tools are not
part of this curated source tree or its default build. See the validation backlog
for the remaining work to publish a portable regression suite.
The [export validation record](docs/BuildValidation.md) lists the checks actually run.

## First launch and setup

Run `poolrad.exe` from the installed PoolRad folder, or use **Pool of Radiance QOL**.
Windows launches without a separate console. Paths resolve beside the executable,
independent of its working directory.

Setup checks the 32-bit Windows GOG registry entry
`HKLM\SOFTWARE\WOW6432Node\GOG.com\Games\1432640961`, value `path`,
and appends POOLRAD. You can edit the detected path or paste another supported
folder path, including spaces, quoted Explorer paths and Unicode characters.

```text
PoolRad/
  poolrad.exe + runtime DLLs
  README.md
  Pool of Radiance QOL.lnk
  pr_setup.log       appended startup/setup diagnostics
  Game/              required DOS resources
  Save/              port campaign saves
    Characters/      standalone character saves
  Text/              extracted game strings and source-location hint
  Journals/          extracted Journal pages and metadata
  licenses/          bundled dependency notices
```

Valid resources and caches skip acquisition/extraction on later launches. Missing
or damaged generated text is repaired. Original source files and existing saves
are not modified by setup. There are no bundled campaign or character saves.

Journal extraction is optional: missing/unsupported PDFs or extraction failures
are logged and **do not prevent play**. Discoveries still record normally, and
unavailable pages show **ENTRY MISSING**. A valid Journal cache no longer needs
the PDF. To retry with the supported PDF explicitly:

```powershell
.\poolrad.exe --qol --journal-pdf "D:\My Games\Adventurers Journal.pdf"
```

## Implemented quality-of-life upgrades

One launch flag, **`--qol`**, enables the current upgrade category:

| Feature | Behaviour |
| --- | --- |
| **Encamp Fix** | Temporarily uses available first-level cleric slots for Cure Light Wounds, casts/rests/repeats as needed, then restores and memorises the original spell selection. Normal time, spell costs, casting restrictions and rest encounters apply; automation stops when blocked or interrupted. |
| **Shorter wilderness travel** | An accepted wilderness step costs two hours instead of the standard twelve. Existing elapsed time is not rewritten. |
| **Training eligibility indicator** | Exploration names turn EGA light blue (`#5555FF`) when a class has enough XP to advance and is not capped, regardless of available gold. Status colours retain precedence. |
| **In-game Journal** | **J** opens discovered Journal entries, council proclamations and Tavern Tales. Unlocks are recorded from authenticated encounter references and retained in saves. The PDF recipe covers all 99 IDs across 144 image pages; normal play reveals only encountered/recoverable records. |

The Journal opens directly to scanned pages. **N/P** changes pages, **Z** toggles
Zoom, arrows pan in Zoom, and Escape returns. Fit uses original-resolution SDL
scaling; later pages cannot exceed the first page's fitted width. Tavern Tales
use at most 70% of the available width.

The load browser's zone names and elapsed `DDD:HH:MM`, readable dialogue
acknowledgements and post-combat automatic-control reset are standard behaviour,
not additional switches. Unfinished upgrades are listed in [OpenQOLWork](docs/OpenQOLWork.md).

## Command-line options

### Play, display and sound

| Option | Purpose |
| --- | --- |
| `--start` | Title/credits and CALE character/party menu; default launch mode. |
| `--qol` | Enable the quality-of-life features above. |
| `--scale 2..6` | Select 640x400, 960x600, 1280x800, 1600x1000 or 1920x1200. F5 cycles presets in-game. |
| `--sound auto\|off\|pc-speaker\|tandy` | Use POOL.CFG's sound choice or an explicit source sound device. |
| `--mute` | Disable audio; equivalent to `--sound off`. |
| `--help` | Produce the built-in option and fixture help. |

### Setup and content paths

| Option | Purpose |
| --- | --- |
| `--setup-from DIR` | Select a DOS POOLRAD source without the setup dialog when Game needs installation. |
| `--install-root DIR` | Put Game/Save/Text/Journals and pr_setup.log under another installation root. |
| `--journal-pdf FILE` | Supply the optional supported GOG Journal PDF. |
| `--journal-data DIR` | Override the Journal image/metadata directory. Use `--journal-pdf` too when explicitly extracting there. |
| `--strings FILE` | Override the generated game string cache. |
| `--data DIR` | Use an explicit developer DOS asset directory, with legacy cache/storage defaults. Cannot be combined with `--install-root` or `--setup-from`. |

### Saves

| Option | Purpose |
| --- | --- |
| `--load-menu` | Open the save browser. |
| `--resume A..J` | Resume a port campaign generation from the chosen slot. |
| `--load A..J` | Read-only original DOS save import; requires the original save and character sidecars, normally through `--data`. |
| `--user-data DIR` | Override legacy storage root: campaigns in DIR/saves and standalone characters in DIR/characters. The default installed layout uses Save and Save/Characters. |

Choose a browser, port resume or original import rather than combining them.
Campaign loads cannot be mixed with scenario/map overrides or inspection modes.
Port saves currently use schema 6 and a compiler-library-specific MT19937 format;
GNU and MSVC saves are not interchangeable. Opening the reader does not advance
campaign time. Keep older committed generations when testing changes.

### Developer inspection and diagnostics

| Option | Purpose |
| --- | --- |
| `--debug-trace DIR` | Opt-in recording; F8 assists combat wins, F9 exports state/screenshot, F10 opens load. |
| `--smoke` | Run three hidden SDL frames with audio disabled. |
| `--snapshot FILE.ppm` | Export one rendered frame and exit. |
| `--viewer` | Open the original-asset viewer. |
| `--archive FILE.DAX` | Select an asset archive and imply viewer mode. |
| `--inspect` | Inspect the supplied asset corpus. |
| `--extract DIR` | Export derived assets into DIR. |
| `--explore` | Enter an exploration preview. |
| `--disk 1..8` | Choose a resource disk for a preview. |
| `--record 0..255` | Choose a map/ECL/archive record, according to mode. |
| `--ecl ECLn.DAX` | Trace a selected ECL record. |
| `--entry 0..4` | Select the ECL entry point. |
| `--budget 1..1000000` | Bound instruction work; default 10000. |
| `--seed 0..4294967295` | Set the initial host RNG seed; default 1. |
| `--trace` | Include detailed ECL execution output. |
| `--vm-corpus` | Inspect the assigned ECL entry points across the corpus. |
| `--scenario NAME` | Run a compiled developer scenario; see built-in help/source for the available names. |
| `--party A\|J` | Import an original character/item fixture, not a complete campaign. Requires its source sidecars. |

Developer inspections need appropriate local game resources. Their extracted
outputs, traces and fixture saves do not belong in a source commit. On Windows,
command output can be captured by scripts; setup diagnostics use pr_setup.log.

## Source layout and contributing

- `src/`, `include/`: GBEngine runtime and public interfaces.
- `config/source_sets.json`: explicit production source inventory consumed by CMake.
- `config/journal-image-recipe.json`: reviewed PDF stream/crop geometry; no page pixels or transcribed text.
- `config/journal-image-overrides.json`: approved per-entry crop overrides.
- `config/journal-references.tsv`: authenticated encounter-address/ID metadata.
- `cmake/`, `scripts/`: dependency discovery, installation and metadata generation.
- `docs/OpenDevWork.md`: parked gaps, validation and non-QoL development work.
- `docs/OpenQOLWork.md`: all five currently open QoL proposals.
- `third_party/`: toml++ header/license and SDL notice; obtain the SDL SDK separately.

Read [AGENTS.md](AGENTS.md) before changing runtime behaviour. Preserve staged
commit/rollback, actual-present acknowledgement barriers, source authentication
and save ownership. Use `python scripts/generate-journal-recipe.py --check` when
editing extraction metadata; Python is a developer tool, not a runtime dependency.
Run `python scripts/check-source-tree.py` before preparing a source commit.

No project-wide LICENSE has been selected yet. Dependency notices retain their
own licenses in `third_party/`. Original game assets remain separately supplied.
