"""Compatibility entry point for the native Mac build; Windows builds are retired."""
import subprocess
import sys
from pathlib import Path

if __name__ == '__main__':
    if sys.platform != 'darwin':
        raise SystemExit('macOS builds now run on a Mac: python3 tools/local-build.py --platform macos')
    subprocess.run([sys.executable, str(Path(__file__).with_name('macos-native-build.py')), *sys.argv[1:]], check=True)
