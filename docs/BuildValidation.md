# Validation of this source export

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
The exported CMake MSVC/package-discovery branch and other hosts still require
application-level validation with matching external SDKs.

## Repeatable source checks

Python is optional for development checks; it is not needed to compile or run:

```text
python scripts/check-source-tree.py
python scripts/generate-journal-recipe.py --check
```

Use the README build/install commands with your own compiler-matched SDL SDK.
The historical workspace's asset-dependent tests are intentionally not registered
as runnable tests in this standalone source export. Publishing a portable suite
is tracked in OpenDevWork.md.
