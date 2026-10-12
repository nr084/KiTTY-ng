# Локальная сборка и версии

[Документация](README.md) | [English](build.en.md)

## Windows

Основная цепочка работает локально на Windows x64. Нужны **Python 3.10+ с pip** и **Git в PATH**, для первой сборки — доступ в интернет. Visual Studio и отдельная установка Windows SDK не требуются: используется LLVM-MinGW. GitHub Actions для сборки не используются.

```powershell
python tools/local-build.py
```

Команда проверяет последний стабильный LLVM-MinGW и устанавливает его локально в `_tools/llvm-mingw/<выпуск>/toolchain`. Установленный компилятор сохраняется между сборками; при появлении нового стабильного выпуска скачивается новая версия. CMake и Ninja подготавливаются во временном каталоге. SHA-256 загрузок проверяются. Цепочка собирает клиент и утилиты для **x64, x86 и ARM64**, выполняет необходимые проверки и создаёт три ZIP в `dist`. Временные зависимости и промежуточные файлы удаляются даже при ошибке; логи остаются в `dist/local-build-logs`, отчёт — в `dist/local-build.json`, контрольные суммы — в `dist/SHA256SUMS.txt`. Команда ничего не публикует на GitHub.

Установить или проверить локальный компилятор отдельно: `python tools/local_toolchain.py`.

Уже успешно пройденные проверки пропускаются, когда исходники, заголовки, тестовые сценарии и компилятор для соответствующей проверки не изменились. Результаты и зависимости сохраняются в `dist/local-tests.json`; файл следует сохранять между сборками. Существующий отчёт `dist/validation.json` также используется как подтверждение ранее пройденных проверок, если текущие зависимости совпадают с указанным коммитом. Первую сборку без такого подтверждения сопровождает полный набор проверок x64 и x86. ARM64 проверяется по структуре PE и версии ресурсов; запуск на устройстве ARM64 нужен отдельно.

Можно собрать только одну архитектуру или использовать собственные инструменты без загрузок:

```powershell
python tools/local-build.py --arch x64
python tools/local-build.py --offline --toolchain C:\Toolchains\llvm-mingw --cmake C:\Tools\cmake.exe --ninja C:\Tools\ninja.exe
```

Для offline-режима установите `paramiko` в Python заранее, если необходима проверка изменённой SSH-логики. При обычной сборке эта зависимость устанавливается во временное окружение только при необходимости. `--force-tests` явно запускает все проверки заново, например для воспроизведения ошибки. Точные версии инструментов и коммит записываются в `BUILD-INFO.json` внутри ZIP. При ошибке сборки или теста цепочка прекращается и записывает причину в отчёт.

Вариант с установленной Visual Studio 2022 и Windows SDK остаётся доступным через CMake presets:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --parallel
```

Проверены работающие сборки x64 и x86. ARM64 собрана и проверена по формату PE; пользователь подтвердил работу ранее полученной ARM64-сборки. Новые сценарии 0.85-4 на ARM64 отдельно не проверены. Сборки не подписаны Authenticode.

Номер редакции KiTTY-ng: `python tools/set-release-stage.py 1` для `0.85-1`, `2` для `0.85-2` и далее. База остаётся равной версии PuTTY. Версию повышать только по отдельной команде на выпуск. После изменения каталога переводов выполните `python tools/generate-translations.py`.

## macOS Universal / DMG

```powershell
python tools/local-build.py --platform macos
python tools/macos-dmg.py
```

Первая команда компилирует все шесть исполняемых файлов для ARM64 и x86_64, объединяет и подписывает их и создаёт промежуточный Universal ZIP. Вторая упаковывает приложение и инструкции в HFS+ DMG, сохраняя права запуска. Отчёты: `dist/macos-build.json` и `dist/macos-dmg.json`. ZIP можно удалить после получения DMG.

Инструменты хранятся в `_tools/macos`; временные каталоги очищаются. Повторная компиляция без сети: `python tools/local-build.py --platform macos --offline`. Проверка структуры и подписи на Windows не заменяет нативный запуск AppKit и Gatekeeper на Mac.

## Правила выпусков

`0.85` — версия импортированной базы PuTTY, число после дефиса — редакция KiTTY-ng: `0.85-1`, `0.85-2`, `0.85-3`, `0.85-4`. Теги: `v0.85-4`. Версии клиента и всех утилит совпадают. Версия добавляется в имена ZIP/DMG, архитектура — в имена Windows EXE; номер версии в имени EXE не используется.

Номер повышается только по отдельной команде на выпуск. Обновление документации не создаёт новый релиз. Старые опубликованные релизы сохраняются.

## Linux · AppImage / AppDir

```powershell
python tools/local-build.py --platform linux
python tools/linux-package-windows.py
```

[Linux guide](linux.en.md) · [Руководство Linux](linux.md).

## Complete release build

```powershell
python tools/release-build.py --build-only
# После нативной проверки Linux:
python tools/release-build.py --assemble-only
```

Builds all platforms locally on Windows, checks committed source provenance and requires matching native Linux acceptance before publication.
