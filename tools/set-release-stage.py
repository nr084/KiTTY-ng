"""Keep kitty-ng's base version identical to the imported PuTTY release."""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("stage", help="test, test-2, test-3, ... or stable")
args = parser.parse_args()
if args.stage != "stable" and not re.fullmatch(r"test(?:-(?:[2-9]|[1-9][0-9]+))?", args.stage):
    parser.error("stage must be test, test-N (N >= 2), or stable")
root = Path(__file__).resolve().parents[1]
header = root / "version.h"
text = header.read_text()
text = re.sub(r'^#define PRERELEASE .*\n', '', text, flags=re.M)
base = re.search(r"^#define RELEASE (\d+\.\d+)$", text, re.M).group(1)
version = base + ("" if args.stage == "stable" else "-" + args.stage)
text = re.sub(r'^#define TEXTVER .*$', f'#define TEXTVER "KiTTY-NG {version}"', text, flags=re.M)
text = re.sub(r'^#define SSHVER .*$', f'#define SSHVER "-Release-{version}"', text, flags=re.M)
if args.stage != "stable":
    text = text.replace(f"#define RELEASE {base}\n", f"#define RELEASE {base}\n#define PRERELEASE 1\n")
header.write_text(text)
(root / "VERSION").write_text(version + "\n")
print(version)
