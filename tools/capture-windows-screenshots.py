"""Refresh selected Windows screenshots using synthetic data."""
import argparse
import os
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PANELS = {
    "session": "Session",
    "language": "Window/Translation",
    "transfer": "Connection/File transfer",
    "automation": "Connection/Automation",
    "extensions": "Window/Extensions",
    "proxies": "Connection/Proxy profiles",
}
SCREENS = (*PANELS, "about", "kittygen")

parser = argparse.ArgumentParser()
parser.add_argument("--build", type=Path, required=True)
selection = parser.add_mutually_exclusive_group(required=True)
selection.add_argument("--only", nargs="+", choices=SCREENS)
selection.add_argument("--all", action="store_true")
parser.add_argument("--language", choices=("ru", "en"))
args = parser.parse_args()

out = ROOT / "docs/screenshots"
build = args.build.resolve()
with tempfile.TemporaryDirectory(prefix=".capture-", dir=out) as temporary:
    temp = Path(temporary)
    env = dict(os.environ, KITTY_NG_PORTABLE_DIR=temporary)
    for lang in (args.language,) if args.language else ("ru", "en"):
        for name in SCREENS if args.all else dict.fromkeys(args.only):
            filename = f"{name}-{lang}.png"
            capture = temp / filename
            exe = build / ("kittygen-x64.exe" if name == "kittygen" else "kitty-ng-x64.exe")
            command = [str(exe), "--language", lang]
            if name == "kittygen":
                command += ["-demo-screenshot", str(capture)]
            else:
                command += ["--portable-dir", temporary]
                if name == "about":
                    command += ["-demo-about", str(capture)]
                else:
                    command += ["-demo-config-box", str(capture), "-demo-panel", PANELS[name]]
            subprocess.run(command, env=env, check=True, timeout=30)

            destination = out / filename
            with Image.open(capture) as source:
                pixels = source.convert("RGB")
            if destination.exists():
                with Image.open(destination) as existing:
                    if pixels.size == existing.size and pixels.tobytes() == existing.convert("RGB").tobytes():
                        continue
            pixels.save(capture, format="PNG", optimize=True)
            capture.replace(destination)
