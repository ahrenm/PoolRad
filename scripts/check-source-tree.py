"""Check the curated source inventory and accidental publication artifacts."""
from pathlib import Path
import json
import os
import re

ROOT = Path(__file__).resolve().parents[1]
groups = json.loads((ROOT / "config/source_sets.json").read_text(encoding="utf-8"))
listed = [source for sources in groups.values() for source in sources]
actual = {path.relative_to(ROOT).as_posix() for path in (ROOT / "src").rglob("*.cpp")}
if len(listed) != len(set(listed)) or set(listed) != actual:
    raise SystemExit("Production source manifest has missing, duplicate or unlisted units")
for source in listed:
    path = Path(source)
    if path.is_absolute() or ".." in path.parts:
        raise SystemExit("Source manifest must stay inside the checkout")
for path in [*(ROOT / "src").rglob("*.cpp"), *(ROOT / "include").rglob("*.hpp")]:
    text = path.read_text(encoding="utf-8")
    for header in re.findall(r'#include "(gbengine/[^"\n]+)"', text):
        if not (ROOT / "include" / header).is_file():
            raise SystemExit(f"Missing public include in {path.relative_to(ROOT)}: {header}")

skip = {".git", ".idea", ".vs", ".vscode", "dist", "out", "PoolRad", "Game", "Save", "Text",
        "Journals", "SourceBinaries", "userdata", "research", "__pycache__"}
blocked = {".exe", ".dll", ".gbs", ".dax", ".sav", ".itm", ".spc", ".ovr", ".pdf",
           ".ppm", ".obj", ".o", ".lib", ".a", ".pdb", ".lnk"}
secret = re.compile(r"ghp_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{30,}|sk-(?:proj-)?[A-Za-z0-9_-]{24,}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----")
private_path = re.compile(r"[A-Za-z]:[/\\](?:Users|Projects)[/\\]", re.I)
files = 0
for directory, dirs, names in os.walk(ROOT):
    dirs[:] = [name for name in dirs if name not in skip and not name.startswith("build")]
    for name in names:
        path = Path(directory) / name
        relative = path.relative_to(ROOT)
        if path.is_symlink() or path.suffix.lower() in blocked:
            raise SystemExit(f"Unexpected publication artifact: {relative}")
        if re.fullmatch(r"(?:entry|tale|proclamation)-\d+-\d+\.png", name):
            raise SystemExit(f"Extracted Journal pixels must not be committed: {relative}")
        text = path.read_text(encoding="utf-8")
        if secret.search(text):
            raise SystemExit(f"Review credential-like content in {relative}")
        if private_path.search(text):
            raise SystemExit(f"Private workspace path in {relative}")
        files += 1
print(f"Source tree verified: {len(listed)} production units; {files} publication files; project includes resolve")
