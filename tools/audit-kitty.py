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
option_source = None
for _, path in files:
    header = stream.readline().split()
    content = stream.read(int(header[2]))
    assert stream.read(1) == b"\n"
    if path == "0.76b_My_PuTTY/putty.h":
        option_source = content.decode("utf-8", errors="replace")
    for switch in set(re.findall(rb"\bMOD_[A-Z0-9_]+\b", content)):
        features.setdefault(switch.decode(), []).append(path)
report = {"source_commit": commit, "scope": "0.76b_My_PuTTY and extension source trees; older duplicate excluded",
          "source_file_count": len(files), "feature_switches": dict(sorted(features.items()))}
assert option_source
extension_options = re.findall(r"X\(\w+,\s*\w+,\s*(\w+)\)",
    option_source[option_source.index("X(STR, NONE, host_alt)"):].split("/* end of list */", 1)[0])
decisions = json.loads((root / "docs/kitty-migration-decisions.json").read_text(encoding="utf-8"))
for field, expected in (("switches", set(features)), ("options", set(extension_options))):
    assigned = [item for group in decisions["decisions"] for item in group.get(field, [])]
    duplicates = sorted(item for item in set(assigned) if assigned.count(item) > 1)
    missing = sorted(expected - set(assigned))
    unknown = sorted(set(assigned) - expected)
    if duplicates or missing or unknown:
        raise SystemExit(f"Incomplete {field}: duplicates={duplicates}, missing={missing}, unknown={unknown}")
assert all(group["status"] in {"ported", "replaced", "retired"} and group["reason"]
           for group in decisions["decisions"])
report.update(extension_options=extension_options, migration_status="closed",
              decisions="kitty-migration-decisions.json")
target = root / "docs/kitty-feature-audit.json"
target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(f"Inventoried {len(files)} source files and {len(features)} MOD_* switches: {target.name}")
