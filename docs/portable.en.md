# Portable profiles and compatibility

[Documentation](README.en.md) | [Русский](portable.md)

First make a backup of your old KiTTY directory. Place kitty-ng in the copy alongside `kitty.ini`, `Sessions` and `SshHostKeys`, or specify the settings directory explicitly:

```powershell
.\kitty-ng-x64.exe --portable-dir "D:\Terminal\KiTTY"
```

The client supports legacy `Key\value\` session files, `%XX` encoding, nested directories, configured filename extensions, and paths from `putty.conf`. Unknown KiTTY fields are retained when a session is saved. The session window opens the main folder. Subfolders appear as `<name>`; double-click to enter, and use `..` to go up. **Quick search** matches names in the current folder without regard to case.

Saved passwords are read from the legacy `Password` field using `cryptsalt`. This includes root login, SSH password authentication and keyboard-interactive password prompts. The format remains compatible with KiTTY and is reversible: **it is not a secure secrets vault**. Someone with access to a session file can recover its password. Do not publish personal `Sessions`, `kitty.ini` files or private keys.

Entire profiles encrypted with a master password are unsupported; export them to ordinary files using the original KiTTY first. Unrecognised non-empty profiles are not overwritten. If a legacy session password cannot be decoded, its original value is preserved; enter the password manually to replace it.

When a portable session is saved, SSH keys and detached SSH certificates inside the portable directory are stored as relative paths, such as `SSH-Keys\client.ppk`. Loading resolves them against the portable directory, independent of the working directory. Move the directory together with its keys: the client, Klink, Kscp and Ksftp continue using the saved references. External keys retain absolute paths. The root defaults to the executable directory; `--portable-dir` or `KITTY_NG_PORTABLE_DIR` selects another portable root. KiTTY file syntax and `%XX` encoding are retained. KiTTYgen and Kageant do not persist key references in sessions; their normal file-opening and file-saving operations keep standard behavior.

The “Update profiles to latest version” button below Delete processes all saved portable sessions, including nested folders. It converts internal absolute key and certificate paths to relative references, preserves all other profile lines, and reports updates and errors. Existing relative references and external keys remain unchanged.

See the [macOS guide](macos.en.md) for shared storage. Mac font and window geometry fields are separate; absolute Windows paths are not converted automatically. Seed files are separate: `kitty-ng-win.rnd` and `kitty-ng-macos.rnd`.
