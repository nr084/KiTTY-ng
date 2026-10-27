# Changelog

## 1.1.0 — 2026-10-27

Крупное функциональное обновление: встроенный SSH вместо внешнего Klink.

- Передача файлов работает внутри WFX без Klink/Plink; использовано существующее ядро PuTTY/KiTTY-ng.
- Пароль, парольная фраза и проверка ключа сервера доступны в самом плагине через Total Commander.
- Сохраняются портативные профили и относительные PPK; EXE клиента нужен только необязательной команде `terminal`.
- Внешние команды прокси отключены; файловое соединение не запускает дочерние процессы.

## 1.0.3 — 2026-10-27

- Точки в отображаемых именах SSH-сеансов заменены на `·`, чтобы Total Commander не переносил окончание имени в колонку расширения.
- Исходные имена Klink, группы и удалённые файлы не меняются. Различаются имена с буквальным `·` и `%`; сохранены старые вложенные пути и плоские закладки с приоритетом текущих видимых имён.
- Добавлены нативные регрессионные проверки дерева и реальных WFX-вызовов для x86/x64.


## 1.0.2 — 2026-10-27

- После завершения WFX-операции очищаются ссылки на временные пути вызывающей стороны, включая стек ANSI-обёртки.

- Ускорен поиск сеансов в широких группах: двоичный поиск вместо полного перебора, с точным сохранением регистра и идентичности подключения.
- Плагин и каталог поставки названы `kitty-ng-fs`; имена DLL — `kitty-ng-fs.wfx` и `kitty-ng-fs.wfx64`. Существующий `kittyngfs.ini` сохранён.
- Отдельные установочный ZIP и архив исходников, SHA-256 для каждого пакета.
- Русское и английское описание, независимый от KiTTY-ng цикл выпусков.
- Сборка x86/x64 на Windows стабильным LLVM-MinGW 20261020; новые регрессионные проверки широкого дерева.


## 1.0.1 — 2026-10-27

- Author: nr084. Homepage: https://github.com/nr084/KiTTY-ng .
- Attribution and homepage in the settings dialog, help, DLL version metadata,
  installer description and documentation; clickable homepage button/command.
- Group folders sort before SSH connection entries in Total Commander's
  normal folder-first view, at the root and at every nested group level.
- SSH entries are now links instead of directories; Enter redirects to the
  same canonical session directory before opening SSH. Names stay unchanged.
- Canonical and legacy flat links open offline; Klink starts when the target
  directory is listed. Existing file transfer and terminal behavior is retained.
- Enforced the SDK's 259-character redirect limit, with a non-destructive
  failure for overlong paths and staged ANSI conversion.
- Added regression checks for entry attributes, activation, long paths and
  rejecting file operations on the connection links themselves.

## 1.0 — 2026-10-27

- Real nested KiTTY-ng groups instead of flattened `%2F` connection labels.
- Original dual-terminal plugin icon, SSH-entry icon and settings gear;
  transparent frames from 16 to 256 pixels embedded in both architectures.
- Standard TC folders for groups and standard icons for remote files.
- Groups and icon requests never start SSH connections.
- Deterministic group/session name collision handling with `[SSH]` suffixes.
- Compatible old flat bookmarks where unambiguous; original Klink load names
  remain unchanged.
- Full hierarchy-aware connection roots, disconnect, symlinks and renames.
- Long combined group/session paths are no longer limited by a flat label.
- Added offline icon, tree navigation and nested real-SSH integration tests.
- Auto-install ZIP retains existing user configuration and x86/x64 tool checks.

## 0.1.0 — 2026-10-27

- Initial C11 WFX plugin, x86 and x64 builds.
- Portable KiTTY-ng profile enumeration and exact executable selection.
- Klink binary transport with no shell, timeouts and cancellation.
- SFTP v3 listing, transfers, safe rename, deletion, mkdir, permissions/times.
- Temporary-file transfer commits, overwrite checks and Unicode paths.
- Russian configuration dialog and portable INI settings.
- Protocol tests, OpenSSH interoperability, profile/path tests and actual
  WFX-to-Klink-to-SSH integration tests.

Known limits are documented in README.ru.md and VALIDATION.md. Version 1.0.1
has not been manually validated in the Total Commander UI in this environment.
