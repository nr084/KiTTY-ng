"""Inventory KiTTY feature switches from the preserved original Git commit.

The older duplicate 0.76_My_PuTTY tree is omitted; this inventory is not a
claim that each MOD_* corresponds to one independent patch.
"""
import io
import json
import re
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
commit = "75fa2abcd220c17249ff7252f8d5224137001f2d"
git = ["git", "-c", "safe.directory=" + str(root)]
tree = subprocess.check_output(git + ["ls-tree", "-rz", commit], cwd=root)
files = []
for record in tree.split(b"\0"):
    if not record: continue
    info, path = record.split(b"\t", 1)
    path = path.decode("utf-8")
    if path.startswith("0.76_My_PuTTY/") or not path.endswith((".c", ".h", ".cpp")):
        continue
    mode, kind, sha = info.split()
    if kind == b"blob": files.append((sha, path))
data = subprocess.check_output(git + ["cat-file", "--batch"],
    input=b"\n".join(sha for sha, _ in files) + b"\n", cwd=root)
stream = io.BytesIO(data)
features = {}
for _, path in files:
    header = stream.readline().split()
    content = stream.read(int(header[2]))
    assert stream.read(1) == b"\n"
    for switch in set(re.findall(rb"\bMOD_[A-Z0-9_]+\b", content)):
        features.setdefault(switch.decode(), []).append(path)
report = {"source_commit": commit, "scope": "0.76b_My_PuTTY and extension source trees; older duplicate excluded",
          "source_file_count": len(files), "feature_switches": dict(sorted(features.items()))}
target = root / "docs/kitty-feature-audit.json"
target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(f"Inventoried {len(files)} source files and {len(features)} MOD_* switches: {target.name}")
