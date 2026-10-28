# KiTTY-ng

**KiTTY-ng 0.85-9 для Windows, Linux и macOS**: общее русско-английское языковое ядро, переводы сообщений SSH и исправления вкладок. [Изменения](docs/releases/0.85-9.md) · [Скачать выпуск](https://github.com/nr084/KiTTY-ng/releases/tag/v0.85-9).

**macOS Universal 2 · Портативный режим**: поддерживаются Apple Silicon и Intel. Профили и ключи хранятся вне приложения; общую портативную папку можно использовать с Windows. Нативные вкладки сеансов включены.

**Плагин Total Commander: [kitty-ng-fs 1.1.1](https://github.com/nr084/KiTTY-ng/releases/tag/kitty-ng-fs-v1.1.1)** — встроенный SSH/SFTP без Klink, с вводом пароля и подтверждением ключа сервера в TC. Плагин и исходники доступны отдельными архивами.

**Русский** | [English](README.en.md)

<img src="icons/kitty-ng-forum.png" width="160" alt="KiTTY-ng">

**SSH, файлы и портативные сеансы на Windows, macOS и Linux.** KiTTY-ng развивает KiTTY на базе PuTTY 0.85: терминал, управление подключениями, ключи и автоматизация с русским и английским интерфейсом. Для Total Commander доступен отдельный SFTP-плагин **kitty-ng-fs**.

**macOS Portable и общие Windows-профили.** Держите `KiTTY-ng.app` рядом с Windows EXE и используйте один каталог `Sessions`. Настройки и ключи остаются снаружи приложения; внутренние файловые ссылки можно хранить относительно общей папки.

## Скачать

| Платформа | Сборка 0.85-9 | Запуск |
| --- | --- | --- |
| Linux x86_64 | [AppImage](https://github.com/nr084/kitty-ng/releases/download/v0.85-9/kitty-ng-0.85-9-x86_64.AppImage) · [AppDir](https://github.com/nr084/kitty-ng/releases/download/v0.85-9/kitty-ng-0.85-9-x86_64.tar.zst) | `chmod +x`, запуск |
| Windows x64 | [ZIP](https://github.com/nr084/kitty-ng/releases/download/v0.85-9/kitty-ng-0.85-9-x64.zip) | `kitty-ng-x64.exe` |
| Windows x86 | [ZIP](https://github.com/nr084/kitty-ng/releases/download/v0.85-9/kitty-ng-0.85-9-x86.zip) | `kitty-ng-x86.exe` |
| Windows arm64 | [ZIP](https://github.com/nr084/kitty-ng/releases/download/v0.85-9/kitty-ng-0.85-9-arm64.zip) | `kitty-ng-arm64.exe` |
| macOS 13+ · Intel / Apple Silicon | [Universal DMG](https://github.com/nr084/kitty-ng/releases/download/v0.85-9/kitty-ng-0.85-9-macos-universal.dmg) | `KiTTY-ng.app` |

[Все релизы и контрольные суммы](https://github.com/nr084/kitty-ng/releases). В комплекте клиент и пять утилит. Имена Windows EXE содержат архитектуру; версия указана внутри программ и в именах архивов.

## Первый запуск

1. Скачайте свою сборку. На Windows распакуйте ZIP; на Mac скопируйте `.app` из DMG в доступную для записи папку и извлеките образ. В Linux сделайте AppImage исполняемым (`chmod +x файл.AppImage`) и запустите его; при отсутствии FUSE используйте AppDir или `--appimage-extract-and-run`.
2. Для существующих настроек разместите клиент рядом с `kitty.ini`, `Sessions` и `SshHostKeys` либо выберите папку профилей. Windows-архивы содержат маркер `kitty-ng.portable`. На Mac это папка, содержащая `KiTTY-ng.app`, а не каталог внутри приложения. Сеансы рядом с `.app` обнаруживаются автоматически; выбранная вручную папка запоминается.
3. Откройте профиль или создайте SSH-сеанс. Выберите язык и нажмите **Применить**.

На Mac используется ad-hoc подпись без Developer ID и нотариального заверения. Если запуск заблокирован: **Настройки системы → Конфиденциальность и безопасность → Всё равно открыть**.

## Возможности

- macOS (ранее выпущенные возможности): нативные вкладки, дублирование временных подключений без записи профиля и верхнее меню активного сеанса. [Подробнее](docs/tabs.md).
- Вкладки Windows/Linux в 0.85-9: дублирование в том же окне, перетаскивание, отдельные кнопки закрытия. [Подробнее](docs/tabs.md).

- Папки сеансов, быстрый поиск без учёта регистра, редактирование и дублирование.
- Формат оригинального KiTTY и переносимые ссылки на ключи и сертификаты.
- SCP/SFTP через kscp, отправка перетаскиванием и скачивание.
- Генератор KiTTYgen, агент Kageant и настройки аутентификации текущего сеанса.
- Автокоманды и UTF-8-скрипты после входа, задержки и переподключение.
- Настройки терминала, горячие клавиши, русский и английский интерфейс.

## Total Commander · kitty-ng-fs

[KiTTY-ng FS](kitty-ng-fs/README.md) — отдельный WFX-плагин для **Windows x86/x64**. Открывает SFTP в панелях Total Commander через встроенное ядро PuTTY/KiTTY-ng и существующие портативные SSH-сеансы: вложенные группы, передача файлов, переименование, каталоги и Unix-права.

[Скачать плагин 1.1.1](https://github.com/nr084/KiTTY-ng/releases/download/kitty-ng-fs-v1.1.1/kitty-ng-fs-1.1.1-x86-x64.zip) · [Исходники отдельно](https://github.com/nr084/KiTTY-ng/releases/download/kitty-ng-fs-v1.1.1/kitty-ng-fs-1.1.1-source.zip) · [Установка и ограничения](kitty-ng-fs/README.md)

**Совместимость имён:** `mail.example.ru` отображается как `mail·example·ru`, без переноса окончания в колонку расширения. Исходное имя подключения и имена удалённых файлов сохраняются; поддерживаются старые пути сеансов.

**Новое в 1.1.1:** скачивание использует очередь запросов для ускорения на сетях с задержкой; журнал показывает переданный объём в МиБ. Файловые соединения работают внутри WFX без внешних EXE. Поддерживаются пароль, keyboard-interactive, PPK и подтверждение серверного ключа через TC. Клиент и плагин выпускаются независимо; установленный KiTTY-ng нужен только для необязательного открытия терминала.

## Интерфейс

### Windows

<img src="docs/screenshots/session-ru.png" width="760" alt="KiTTY-ng — Windows">

### macOS Universal

<img src="docs/screenshots/macos-tabs-ru.png" width="760" alt="KiTTY-ng — macOS">

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
- [Изменения 0.85-9](docs/releases/0.85-9.md) · [Следующие задачи](docs/planned.md).

## Совместимость и проверка

Сохранённые пароли KiTTY имеют обратимый формат; это не защищённое хранилище. Полностью зашифрованные профили с мастер-паролем не поддерживаются. Исключённые расширения перечислены в [матрице решений](docs/kitty-migration-decisions.json).

Пользователь подтвердил работу на MacBook Air M3 и Windows ARM64. Автоматические нативные проверки Windows выполнены на x64/x86. Текущая локальная сборка macOS собирается и проверяется на Mac: ARM64 и x86_64 через Rosetta, SSH, ключи, передачи и AppKit. Физический Intel Mac, macOS 13, GSSAPI и реальный SSH CA handshake требуют отдельной проверки. Подробности — [нативный отчёт Mac](docs/macos-native-validation.md).

## Благодарности и лицензии

Спасибо **Саймону Тэтэму (Simon Tatham)** и всем участникам [PuTTY](https://www.chiark.greenend.org.uk/~sgtatham/putty/): их работа составляет основу KiTTY-ng. Отдельная благодарность **Сирилю Дюпону (Cyril Dupont, cyd01)**, автору [KiTTY](https://github.com/cyd01/KiTTY), за расширения и удобство портативного клиента, совместимость с которым мы сохраняем.

Оригинальные уведомления об авторских правах и тексты лицензий PuTTY и KiTTY сохранены в отдельных файлах. KiTTY-ng — самостоятельный проект nr084, продолжающий эту работу.

База: [PuTTY 0.85](https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html). Расширения и совместимость: [KiTTY](https://github.com/cyd01/KiTTY), исходный коммит `75fa2abcd220c17249ff7252f8d5224137001f2d`. История KiTTY сохранена в Git.

См. [лицензию PuTTY](LICENCE) и [лицензию KiTTY](LICENCE-KiTTY.txt). Новый значок с двумя соединёнными терминалами: [исходное изображение](icons/kitty-ng.png), [сведения о генерации](icons/kitty-ng-prompt.txt).

[Общее языковое ядро и правила перевода](docs/localization.md): русский и английский встроены; дополнительные языковые DLL отложены.
