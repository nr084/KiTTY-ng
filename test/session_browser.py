"""Isolated portable folder fixture; never reads user settings."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="kitty-ng-browser-") as temporary:
    root = Path(temporary)
    for name in ("empty", "prod/sub", "space%20folder"):
        (root / "Sessions" / name).mkdir(parents=True)
    (root / "Sessions/session-file").write_text("HostName\\localhost\\\n")
    environment = os.environ.copy()
    environment["KITTY_NG_PORTABLE_DIR"] = str(root)
    subprocess.run([sys.argv[1]], env=environment, check=True)
