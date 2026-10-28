"""Compatibility entrypoint: translations now live in locale/ru.json."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name("generate-translations.py")), run_name="__main__")
