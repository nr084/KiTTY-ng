"""Keep kitty-ng's base version identical to the imported PuTTY release."""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("stage", help="KiTTY-ng revision: 1, 2, ...; legacy test/test-N/stable also accepted")
args = parser.parse_args()
revision = int(args.stage) if re.fullmatch(r"[1-9][0-9]*", args.stage) else None
is_test = bool(re.fullmatch(r"test(?:-(?:[2-9]|[1-9][0-9]+))?", args.stage))
if revision is not None and revision > 65535:
    parser.error("Windows version revision must be at most 65535")
if revision is None and args.stage != "stable" and not is_test:
    parser.error("stage must be a positive revision number, test, test-N, or stable")
root = Path(__file__).resolve().parents[1]
header = root / "version.h"
text = header.read_text()
text = re.sub(r'^#define PRERELEASE .*\n', '', text, flags=re.M)
base = re.search(r"^#define RELEASE (\d+\.\d+)$", text, re.M).group(1)
version = base + ("" if args.stage == "stable" else "-" + args.stage)
text = re.sub(r'^#define TEXTVER .*$', f'#define TEXTVER "KiTTY-ng {version}"', text, flags=re.M)
text = re.sub(r'^#define SSHVER .*$', f'#define SSHVER "-Release-{version}"', text, flags=re.M)
major, minor = map(int, base.split("."))
text = re.sub(r'^#define BINARY_VERSION .*$',
              f'#define BINARY_VERSION {major},{minor},{revision or 0},0', text, flags=re.M)
if is_test:
    text = text.replace(f"#define RELEASE {base}\n", f"#define RELEASE {base}\n#define PRERELEASE 1\n")
header.write_text(text)
(root / "VERSION").write_text(version + "\n")
print(version)
