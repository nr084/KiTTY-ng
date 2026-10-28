"""Native storage regression with isolated portable and registry roots."""
import pathlib, subprocess, sys, tempfile, uuid, winreg
binary = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='kitty-quick-test-') as directory:
    ini = pathlib.Path(directory) / 'kitty.ini'
    ini.write_text('[KiTTY]\nunknown=Сохранить\n[Other]\ntext=still here\n', encoding='utf8')
    subprocess.run([str(binary), directory], check=True)
    text = ini.read_text(encoding='utf16')
    assert text.count('[QuickCommands]') == 1, text
    assert 'unknown=Сохранить' in text and 'text=still here' in text
    key = 'Software\\KiTTY-ng-test-' + uuid.uuid4().hex
    winreg.CreateKey(winreg.HKEY_CURRENT_USER, key).Close()
    def remove(path):
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_ALL_ACCESS) as handle:
            while True:
                try: child = winreg.EnumKey(handle, 0)
                except OSError: break
                remove(path + '\\' + child)
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, path)
    try:
        subprocess.run([str(binary), directory, key], check=True)
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key+'\\Software\\KiTTY-NG\\QuickCommands') as handle:
            assert winreg.QueryInfoKey(handle)[1] == 0
        assert ini.read_text(encoding='utf16') == text
    finally:
        remove(key)
print('PASS: portable INI and isolated registry; unrelated settings preserved')
