# KiTTY-ng FS icon assets

Original vector artwork for KiTTY-ng FS 1.0, under the project's MIT license.
Author: nr084. Homepage: https://github.com/nr084/KiTTY-ng .
No pixels or paths from KiTTY-ng's own icons have been copied.

The plugin's double-terminal motif deliberately follows the visual vocabulary
of the KiTTY-ng client: dark terminal screens, green command prompts, pale
frames and a blue connection. It remains distinct original artwork.

- `plugin.ico`: main plugin icon, two linked terminals.
- `ssh.ico`: saved SSH connection, one blue terminal with a connect arrow.
  The arrow denotes an entry action, not live/online connection status.
- `settings.ico`: configuration gear.
- Normal group folders should retain Total Commander's own folder icon.
  `group-preview.svg` is only a stand-in for comparison in the preview.

Each ICO contains transparent 32-bit frames at 16, 20, 24, 32, 40, 48, 64,
128 and 256 pixels. Small terminal frames use hand-adjusted vector geometry
for small list icons. Frames below 128 pixels are classic BGRA DIBs with
transparency masks; large frames are PNG encoded.

`icons-preview.png` compares the icons at their actual 16/24/32/48px sizes on
light and dark backgrounds. It contains synthetic labels only.

To rebuild, install Python 3 + Pillow and Inkscape, then run:

```sh
python3 tools/build_icons.py
```

The script checks all nine ICO frames with an independent decoder. SVGs are
the source of truth; the user does not need any rendering dependency to use
the compiled WFX plugin.
