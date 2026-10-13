# KiTTY-ng

**Русский** | [English](README.en.md)

<img src="icons/kitty-ng-forum.png" width="160" alt="KiTTY-ng">

**Портативный SSH-клиент для Windows, macOS и Linux.** Развитие KiTTY на базе PuTTY 0.85: русский и английский интерфейс, управление сеансами, SSH-ключи, передача файлов и автоматизация.

**macOS Portable и общие Windows-профили.** Держите `KiTTY-ng.app` рядом с Windows EXE и используйте один каталог `Sessions`. Настройки и ключи остаются снаружи приложения; внутренние файловые ссылки можно хранить относительно общей папки.

## Скачать

| Платформа | Сборка 0.85-5 | Запуск |
| --- | --- | --- |
| Linux x86_64 | [AppImage](https://github.com/nr084/kitty-ng/releases/download/v0.85-5/kitty-ng-0.85-5-x86_64.AppImage) · [AppDir](https://github.com/nr084/kitty-ng/releases/download/v0.85-5/kitty-ng-0.85-5-x86_64.tar.zst) | `chmod +x`, launch |
| Windows x64 | [ZIP](https://github.com/nr084/kitty-ng/releases/download/v0.85-5/kitty-ng-0.85-5-x64.zip) | `kitty-ng-x64.exe` |
| Windows x86 | [ZIP](https://github.com/nr084/kitty-ng/releases/download/v0.85-5/kitty-ng-0.85-5-x86.zip) | `kitty-ng-x86.exe` |
| Windows arm64 | [ZIP](https://github.com/nr084/kitty-ng/releases/download/v0.85-5/kitty-ng-0.85-5-arm64.zip) | `kitty-ng-arm64.exe` |
| macOS 13+ · Intel / Apple Silicon | [Universal DMG](https://github.com/nr084/kitty-ng/releases/download/v0.85-5/kitty-ng-0.85-5-macos-universal.dmg) | `KiTTY-ng.app` |

[Все релизы и контрольные суммы](https://github.com/nr084/kitty-ng/releases). В комплекте клиент и пять утилит. Имена Windows EXE содержат архитектуру; версия указана внутри программ и в именах архивов.

## Первый запуск

1. Скачайте свою сборку. На Windows распакуйте ZIP; на Mac скопируйте `.app` из DMG в доступную для записи папку и извлеките образ. В Linux сделайте AppImage исполняемым (`chmod +x файл.AppImage`) и запустите его; при отсутствии FUSE используйте AppDir или `--appimage-extract-and-run`.
2. Для существующих настроек разместите клиент рядом с `kitty.ini`, `Sessions` и `SshHostKeys` либо выберите папку профилей. Windows-архивы содержат маркер `kitty-ng.portable`.
3. Откройте профиль или создайте SSH-сеанс. Выберите язык и нажмите **Применить**.

На Mac используется ad-hoc подпись без Developer ID и нотариального заверения. Если запуск заблокирован: **Настройки системы → Конфиденциальность и безопасность → Всё равно открыть**.

## Возможности

- Папки сеансов, быстрый поиск без учёта регистра, редактирование и дублирование.
- Формат оригинального KiTTY и переносимые ссылки на ключи и сертификаты.
- SCP/SFTP через kscp, отправка перетаскиванием и скачивание.
- Генератор KiTTYgen, агент Kageant и настройки аутентификации текущего сеанса.
- Автокоманды и UTF-8-скрипты после входа, задержки и переподключение.
- Настройки терминала, горячие клавиши, русский и английский интерфейс.

## Интерфейс

### Windows

<img src="docs/screenshots/session-ru.png" width="760" alt="KiTTY-ng — Windows">

### macOS Universal

<img src="docs/screenshots/macos-session-ru.png" width="760" alt="KiTTY-ng — macOS">

<details>
<summary>KiTTYgen — macOS</summary>

<img src="docs/screenshots/macos-kittygen-ru.png" width="760" alt="KiTTYgen — macOS">

</details>

### Linux · GTK 3

<img src="docs/screenshots/linux-session-ru.png" width="760" alt="KiTTY-ng — Linux">

## Руководства

- [Документация](docs/README.md).
- [Windows](docs/windows.md) · [macOS](docs/macos.md) · [Linux](docs/linux.md).
- [Портативные профили и переход с KiTTY](docs/portable.md).
- [Локальная сборка](docs/build.md) · [Перенос расширений](docs/migration.md).
- [Изменения 0.85-5](docs/releases/0.85-5.md) · [Следующие задачи](docs/planned.md).

## Совместимость и проверка

Сохранённые пароли KiTTY имеют обратимый формат; это не защищённое хранилище. Полностью зашифрованные профили с мастер-паролем не поддерживаются. Исключённые расширения перечислены в [матрице решений](docs/kitty-migration-decisions.json).

Пользователь подтвердил работу на MacBook Air M3 и Windows ARM64. Автоматические нативные проверки Windows выполнены на x64/x86. Отдельные сценарии GSSAPI, SSH CA и Intel runtime на Mac требуют проверки; подробности — [область проверки](docs/validation.md).

## Благодарности и лицензии

Спасибо **Саймону Тэтэму (Simon Tatham)** и всем участникам [PuTTY](https://www.chiark.greenend.org.uk/~sgtatham/putty/): их работа составляет основу KiTTY-ng. Отдельная благодарность **Сирилю Дюпону (Cyril Dupont, cyd01)**, автору [KiTTY](https://github.com/cyd01/KiTTY), за расширения и удобство портативного клиента, совместимость с которым мы сохраняем.

Оригинальные уведомления об авторских правах и тексты лицензий PuTTY и KiTTY сохранены в отдельных файлах. KiTTY-ng — самостоятельный проект nr084, продолжающий эту работу.

База: [PuTTY 0.85](https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html). Расширения и совместимость: [KiTTY](https://github.com/cyd01/KiTTY), исходный коммит `75fa2abcd220c17249ff7252f8d5224137001f2d`. История KiTTY сохранена в Git.

См. [лицензию PuTTY](LICENCE) и [лицензию KiTTY](LICENCE-KiTTY.txt). Новый значок с двумя соединёнными терминалами: [исходное изображение](icons/kitty-ng.png), [сведения о генерации](icons/kitty-ng-prompt.txt).
