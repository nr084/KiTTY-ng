import json
import subprocess
import sys
from pathlib import Path

vectors = json.loads(Path(__file__).with_name("kittyrus-legacy-vectors.json").read_text())
data = ''.join(f"{v['plain_hex']} {v['key_hex'] or '-'} {v['encoded']}\n" for v in vectors)
subprocess.run([str(Path(sys.argv[1]).resolve())], input=data, text=True, check=True)
