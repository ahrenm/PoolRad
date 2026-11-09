"""Emit committed native Journal metadata from reviewed IDs/geometry; no PDF/OCR."""
from pathlib import Path
import argparse
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[1]


def write_text(path, text):
    """Keep existing line endings, including the recipe's byte fingerprint."""
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return
    crlf = path.exists() and b"\r\n" in path.read_bytes()
    path.write_bytes(text.replace("\n", "\r\n").encode("utf-8") if crlf else text.encode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify without writing files")
    check = parser.parse_args().check
    recipe_path = ROOT / "config/journal-image-recipe.json"
    recipe = json.loads(recipe_path.read_text(encoding="utf-8"))
    overrides = json.loads((ROOT / "config/journal-image-overrides.json").read_text(encoding="utf-8"))
    before = json.dumps(recipe, sort_keys=True)
    records = {record["id"]: record for record in recipe["records"]}
    if len(records) != 99 or not set(overrides) <= records.keys():
        raise ValueError("Expected 99 unique records and known override IDs")
    for identity, pages in overrides.items():
        records[identity]["pages"] = pages
    changed = json.dumps(recipe, sort_keys=True) != before
    if changed and check:
        raise ValueError("Reviewed overrides differ from the committed recipe; regenerate first")
    if not re.fullmatch(r"[0-9a-f]{64}", recipe["pdf_sha256"]):
        raise ValueError("Invalid PDF identity")
    streams = recipe["streams"]
    page_numbers = {stream["page"] for stream in streams}
    if len(page_numbers) != len(streams):
        raise ValueError("Duplicate PDF streams")
    fragments, images = [], []
    for record in recipe["records"]:
        identity = record["id"]
        if not re.fullmatch(r"(?:entry|proclamation|tale)-[0-9]{3}", identity):
            raise ValueError("Invalid record identity")
        if not 1 <= len(record["pages"]) <= 8:
            raise ValueError("Expected one to eight images per record")
        for index, parts in enumerate(record["pages"], 1):
            if not parts:
                raise ValueError("Empty crop page")
            width, height = 0, 12 * (len(parts) - 1)
            for page, left, top, right, bottom in parts:
                if page not in page_numbers or not (0 <= left < right <= 1328 and 0 <= top < bottom <= 1003):
                    raise ValueError("Crop is outside the authenticated scan")
                width = max(width, right - left)
                height += bottom - top
            if width > 2048 or height > 2048:
                raise ValueError("Crop exceeds the native PNG decoder limits")
            images.append((identity, index, len(fragments), len(parts)))
            fragments.extend(parts)
    references = (ROOT / "config/journal-references.tsv").read_text(encoding="utf-8")
    if ')REFS"' in references:
        raise ValueError("Invalid raw-string delimiter in reference metadata")
    if changed:
        write_text(recipe_path, json.dumps(recipe, indent=2) + "\n")
    lines = ["// Generated from reviewed geometry. No journal text or PDF parser.\n",
             f'constexpr std::string_view pdf_hash="{recipe["pdf_sha256"]}";\n',
             f'constexpr std::string_view recipe_hash="{hashlib.sha256(recipe_path.read_bytes()).hexdigest()}";\n',
             "constexpr PdfImageStream streams[]{\n"]
    lines += [" {" + ",".join(str(stream[key]) for key in ("page", "offset", "length")) + "},\n" for stream in streams]
    lines += ["};\nconstexpr CropFragment fragments[]{\n"]
    lines += [" {" + ",".join(map(str, part)) + "},\n" for part in fragments]
    lines += ["};\nconstexpr ScannedPage pages[]{\n"]
    lines += [f' {{"{identity}",{number},{first},{count}}},\n' for identity, number, first, count in images]
    lines += ['};\nconstexpr std::string_view journal_references=R"REFS(' + references + ')REFS";\n']
    outputs = {
        ROOT / "src/platform/journal_image_recipe.inc": "".join(lines),
        ROOT / "src/qol/journal_reference_registry.inc":
            '// Generated encounter reference metadata; no Journal prose.\nconstexpr std::string_view builtin_references=R"REFS(' + references + ')REFS";\n',
    }
    for path, text in outputs.items():
        if check:
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                raise ValueError(f"Stale metadata: {path.relative_to(ROOT)}")
        else:
            write_text(path, text)
    print(f"Journal metadata {'verified' if check else 'generated'}: 99 IDs, {len(images)} pages, {len(fragments)} fragments")


if __name__ == "__main__":
    main()
