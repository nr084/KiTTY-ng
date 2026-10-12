"""Install verified CMake/Ninja wheels locally for native Windows macOS builds."""
import importlib.util
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("local_build", ROOT/"tools/local-build.py")
pipeline = importlib.util.module_from_spec(spec); spec.loader.exec_module(pipeline)
metadata = {}
for package in ("cmake", "ninja"):
    dest = ROOT/"_tools/macos"/package; dest.mkdir(parents=True,exist_ok=True)
    info = dest/"tool-info.json"
    if info.is_file():
        metadata[package] = json.loads(info.read_text()); print("REUSE:", package, metadata[package]["version"], flush=True)
    else:
        metadata[package] = pipeline.fetch_wheel(package,dest)
        info.write_text(json.dumps(metadata[package],indent=2)+"\n")
        print("Installed:", package, metadata[package]["version"], flush=True)
(ROOT/"_tools/macos/host-tools-info.json").write_text(json.dumps(metadata,indent=2)+"\n")
