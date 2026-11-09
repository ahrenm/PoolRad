# Committed build and Journal metadata

`source_sets.json` lists every production CPP in its runtime ownership block.
CMake does not scan parent workspaces or add historical test executables.

`journal-image-recipe.json` is the reviewed final PDF stream/crop table.
`journal-image-overrides.json` records the approved per-entry crop changes.
`journal-references.tsv` contains ECL source hashes, addresses, resolvers and
Journal IDs. None of these files contains PDF pixels or transcribed entry text.

After changing reviewed geometry or references, run:

```text
python scripts/generate-journal-recipe.py
python scripts/generate-journal-recipe.py --check
```

The generator applies overrides, fingerprints the actual recipe bytes, and emits
the two committed `.inc` files used by native extraction and discovery. These
outputs are committed so ordinary CMake builds need no Python or original assets.
Line-ending attributes preserve the recipe fingerprint across Git checkouts.
Review newly extracted pages against the user's authenticated PDF before treating
geometry changes as correct; metadata validation alone does not prove a crop's
visual content or page-break placement.
