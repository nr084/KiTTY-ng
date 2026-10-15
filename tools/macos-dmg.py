"""Compatibility command: package and validate the native Mac build as a DMG."""
import argparse
import subprocess
import sys
from pathlib import Path

if __name__ == '__main__':
    if sys.platform != 'darwin':
        raise SystemExit('macOS packaging now runs on a Mac via tools/local-build.py --platform macos')
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'dist/macos-native')
    args=parser.parse_args()
    subprocess.run([sys.executable,str(Path(__file__).with_name('macos-native-build.py')),'--offline','--output',str(args.output)],check=True)
