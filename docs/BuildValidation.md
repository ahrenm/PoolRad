# Validation of this source export

## Pinned SDL3 source build

The default CMake path now fetches the official SDL3 3.4.20 source archive and
verifies SHA256 `f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f`.
The pin is committed in [SDL3.cmake](../cmake/SDL3.cmake). SDL is built as a shared
library with the application's toolchain; upstream examples/tests and SDL's
separate installation are disabled. An explicit external-SDK opt-out remains.
No gameplay or rendering implementation changed.

Local Windows x64 validation completed:

- A fresh network download passed the archive checksum. A subsequent disconnected
  configuration reused the cached source without downloading it again.
- Full Release builds and application links passed with MinGW GCC 16.1 and MSVC
  19.51, retaining strict warnings for all 150 PoolRad production units. MSVC
  reused the already authenticated SDL source and built its own SDL DLL.
- Both CMake installs passed into separate paths containing spaces. Each staged
  the locally built SDL DLL, its matching source license, README and QoL shortcut;
  MinGW compiler runtime DLLs were staged by the existing packaging rule. Both
  installed SDL DLLs reported version 3.4.20 and matched their build outputs.
- The asset-free startup_help CTest passed for both builds. Both installed
  executables also produced help without installed game assets.
- Both installed applications completed fresh setup with explicit local DOS
  assets in installation paths containing spaces and Unicode. Missing Journal
  PDFs were nonfatal; source authentication appeared in pr_setup.log.
- Both applications extracted all 144 Journal PNGs from the supported local PDF;
  every PNG matched the approved artwork. Cached launches preserved image and
  text-cache timestamps. Bundled DOS save/character files were omitted.
- The GNU package resumed the complete feature-test E11 save and exported a
  snapshot without changing the save. Foreign compiler save interchange is not
  claimed or enabled.
- Private GNU integration checks using the new SDL DLL passed Journal on/off,
  original-resolution Fit, F5, Zoom/pan, category navigation and exact return to
  exploration. Another 1,786 checks covered all 99 IDs/144 pages, width caps,
  software SDL rendering, invalid-PDF rejection and missing-page handling.

These are selected local checks with private fixtures, not a full regression
suite, hardware-renderer comparison or new campaign replay. The narrow-argv
private Journal harness needed an ASCII fixture path; the installed application's
Unicode setup path passed. No proprietary fixtures or generated pixels are
included here. Clean-machine MSVC runtime redistribution and other hosts remain
open; the MSVC smoke checks used this development machine's installed runtime.

## Initial export validation (external SDK)

The curated tree has been configured and built independently of the original
workspace CMake files. Its build inputs are its own src/include/config and
vendored toml++ header, plus an explicitly supplied external SDL SDK. No original
DOS assets, PDF, private research file or fixture is a compilation dependency.

## Checks completed

- All 150 production CPP files, public headers and generated runtime .inc files
  matched the current promoted source at export. There were no gameplay edits.
- A fresh Windows x64/MinGW GCC 16.1 Release build passed strict warnings across
  all runtime libraries and the application, using the SDL SDK fallback path.
- CMake install passed into a separate package directory containing spaces. It
  staged SDL/compiler DLLs, both dependency notices, README and the QoL shortcut.
- The staged executable produced help without installed game assets.
- With explicitly supplied local DOS resources, fresh setup passed while an
  absent Journal PDF was logged as nonfatal. START.EXE/GAME.OVR checks were logged.
- Supplying the authenticated PDF then recovered all144 images; every generated
  PNG matched the approved release artwork bytes.
- The complete feature-test E11 save resumed from a private test installation and
  rendered a snapshot. No fixture/save was added to this source repository.
- All34 parsed runtime switch names are documented in the new README.
- Metadata consistency, source inventory, include resolution and accidental
  artifact/private-path/credential-pattern audits passed.

The runtime exercises used local proprietary inputs outside this repository.
Their bytes and private investigation logs are not included. This is selected
export/build/setup validation, not a fresh full regression suite, complete
campaign replay, original DOS comparison or hardware-driver parity claim.
The external-SDK MSVC/package-discovery branch and other hosts still require
application-level validation with matching SDKs. The newer default source-build
MSVC checks are recorded above.

## Repeatable source checks

Python is optional for development checks; it is not needed to compile or run:

```text
python scripts/check-source-tree.py
python scripts/generate-journal-recipe.py --check
ctest --test-dir build -C Release --output-on-failure
```

Use the README build/install commands; the default fetches/builds pinned SDL3.
The registered startup_help CTest requires no game resources.
The historical workspace's asset-dependent tests are intentionally not registered
as runnable tests in this standalone source export. Publishing a portable suite
is tracked in OpenDevWork.md.
