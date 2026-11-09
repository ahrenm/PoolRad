# Working in PoolRad

## Scope and current status

This is the curated source tree for the current PoolRad/GBEngine application,
not the original investigation workspace. The accepted human-led campaign run
reaches the ending and returns to Phlan. That does not prove every branch or
complete native parity. Read README.md, docs/OpenDevWork.md and docs/OpenQOLWork.md.
There is no approved Phase 5; the latter files are the open work lists.

## Source and content boundaries

- Original DOS resources and the Journal PDF are user-supplied, immutable inputs.
  Never commit original executables/archives, extracted game text/PNG pages,
  source-derived binaries, campaign/character saves, traces or investigation dumps.
- Keep generated Game, Save, Text and Journals inside the executable's writable
  install root. Explicit --data remains a development path; never write caches
  or saves into that read-only asset directory.
- The supported START.EXE/GAME.OVR profiles, ECL reference hashes and PDF extraction
  recipe are deliberate authentication boundaries. Do not bypass an unknown hash
  or silently OCR/guess a different PDF layout.
- Journal .inc files and config metadata contain IDs, addresses, geometry and
  fingerprints, not Journal prose or image pixels. Regenerate them with
  `python scripts/generate-journal-recipe.py`; `--check` verifies consistency.
- Preserve dependency notices. No project-wide LICENSE has been selected yet.

## Architecture

- Runtime namespace is GBEngine. Public interfaces are under include/gbengine;
  implementations mirror them under src. Register every production CPP in
  config/source_sets.json; CMake consumes the explicit inventory.
- Keep main bootstrap-only, App as the composition/ownership root, SDL and native
  OS operations in platform. Use IGameMode/IGameFlow and the shared GameLoop.
- Distinguish DAX decompression, filename-family decoding, ECL scripting,
  campaign state, tactical state and presentation. Do not create a second mutable
  quest/party state beside the authoritative session/VM.
- Original game strings come from launch-extracted StringStore. Do not embed
  recovered game-content text in C++ or the executable. Host diagnostic messages
  and interface explanations are separate from source content.
- Reference rendering is 320x200 with nearest glyph/chrome scaling. Journal Fit
  supplies borrowed original pixels to a separate linear SDL texture; preserve
  its lifetime, physical letterboxing, first-page width cap and 70% Tales cap.

## Behaviour and ownership

- Recover rules from the selected original data/code. Sequel ports and general
  AD&D rules provide hypotheses rather than authority for this build.
- Keep unknown opcodes, fields, callbacks and actor contexts explicit. Do not
  acknowledge a held request merely to make gameplay continue.
- Implement shared behavioural handlers rather than encounter/monster/map-name
  workarounds. Preserve signed widths, native draw order, counters and source
  pre-move/commit/time/post-move order.
- Stage transactions. Late failure must preserve party/effects/map state, item
  identity, expected revisions, RNG, resources and the suspended ECL owner.
- Retain actual-present acknowledgement barriers, readable narration, source
  delays and once-only request completion. Frame time is not campaign time.
- Preserve owned item/effect identities and bindings. Do not replay equipment
  boosts on restore, infer empty callbacks, or fabricate freed-node contents.
- Schema6 saves carry compiler-library-specific MT19937 state. Do not silently
  reinterpret foreign saves or reset RNG. Source import and port resume are
  distinct operations; save only stable restored exploration state.
- Preserve the explicit gaze/fear/thorn/breath compatibility policies listed in
  OpenDevWork. They do not resolve every inherited-scratch ownership question.

## QoL and setup

- --qol controls Fix, shortened wilderness travel, training eligibility and
  Journal access. Unrelated fixes should not change that flag's rules.
- Fix must retain real casting/memorisation/rest costs and interruptions, and
  restore the original spell selection. Rendering eligibility stays read-only.
- Optional PDF extraction failure must not prevent play. Discoveries remain
  source-authenticated even without artwork; the viewer shows ENTRY MISSING and
  safely returns to its known list. Log setup failures to pr_setup.log.
- Windows packaging is a GUI application, with Unicode command-line/path handling
  and no setup console. Install creates the Pool of Radiance QOL shortcut with
  --qol. Do not bundle personal/test saves in a clean release.

## Build and verification

Configure/build using README.md. Do not hardcode a developer drive, username,
workspace SDK, research file or stale object path. CMake fetches/builds pinned
SDL3 3.4.20 by default; keep both URL and SHA256 fixed and validated. External
SDKs require POOLRAD_FETCH_SDL3=OFF; toml++ is vendored. Keep strict compiler
warnings enabled.
Use fresh build directories when changing compilers or public layouts.

The asset-free startup_help CTest checks executable/DLL startup. There is no
advertised public full regression suite yet: the earlier corpus
suites require private/proprietary fixtures. Check metadata consistency, compile
all affected producers/consumers and run meaningful startup/runtime checks with
explicit local assets. When adding tests, keep portable logic separate from
optional local-data fixtures. Never add copyrighted fixtures just to make CI pass.

Report exactly what was tested: source/static evidence, synthetic source points,
assisted campaign steps and actual original-runtime observations are different
claims. A smoke test is not a full campaign replay or hardware parity proof.
