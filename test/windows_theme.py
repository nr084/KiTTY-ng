"""Native theme transitions with isolated preferences; never change system theme."""
import os,subprocess,sys,tempfile
from pathlib import Path
exe=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="kitty-ng-theme-") as temporary:
 root=Path(temporary);env=dict(os.environ,KITTY_NG_PORTABLE_DIR=str(root))
 (root/"kitty.ini").write_text("[KiTTY]\nUITheme=1\nSentinel=keep\n",encoding="utf-8")
 subprocess.run([str(exe),"portable"],env=env,check=True,timeout=20)
 data=(root/"kitty.ini").read_bytes()
 text=data.decode("utf-16") if data.startswith(b"\xff\xfe") else data.decode("utf-8")
 assert "UITheme=0" in text and "Sentinel=keep" in text,text
 env.pop("KITTY_NG_PORTABLE_DIR",None)
 subprocess.run([str(exe),"registry"],env=env,check=True,timeout=20)
