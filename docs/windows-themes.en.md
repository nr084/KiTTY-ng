# Interface themes

Choose **Window → Appearance → Interface theme**: System theme (default), Light or Dark. The choice applies immediately to the client, KiTTYgen and Kageant, and is stored globally rather than inside session profiles. Windows high contrast takes precedence; changing the interface theme preserves terminal colors.

Portable installations use `[KiTTY] UITheme=0|1|2` in `kitty.ini`: 0 follows Windows, 1 is light, 2 is dark. Installed copies use the `UITheme` DWORD under `HKCU\Software\KiTTY-NG`. Open application windows receive a theme-change notification.

Linux GUI utilities follow the desktop GTK theme. Console Klink, Kscp and Ksftp inherit their terminal appearance. macOS uses native system appearance and is built and checked separately on Mac.

[Русский](windows-themes.md)
