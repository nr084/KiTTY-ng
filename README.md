# KiTTY-RUS

SSH, Telnet и последовательный терминал для Windows на базе PuTTY 0.85 с русским и английским интерфейсом и переносимыми настройками KiTTY.

![Два соединённых терминала](icons/kitty-rus.png)

**Тестовая версия: 0.85-test.** Перенос расширений KiTTY продолжается. Это новая база PuTTY с адаптированными расширениями, а не полная копия всех возможностей старого KiTTY. Статус переноса описан в [документе миграции](docs/migration.md).

## Скачать и запустить

Готовые сборки находятся в [релизах](https://github.com/nr084/kitty-rus/releases). Выберите Windows **x64**, **x86** или **ARM64**, распакуйте архив и запустите `kitty-rus.exe`. Установка не требуется. В комплект входят Plink, PSCP, PSFTP, PuTTYgen и Pageant.

Архивы поставляются в портативном режиме: файл `kitty-rus.portable` включает хранение рядом с программой. Для хранения в реестре удалите этот маркер и убедитесь, что в `kitty.ini` нет `savemode=dir`. В обычном режиме используется отдельная ветка `HKCU\Software\KiTTY-RUS`.

## Язык интерфейса

Откройте **Окно → Внешний вид → Язык интерфейса** и выберите русский или английский. Выбор запоминается. Можно также запускать `kitty-rus.exe --language ru` или `--language en`.

Переведены панели настройки, меню, стандартные диалоги и сообщения клиента. Имена сессий, ключи сервера, протокольные идентификаторы и текст удалённого терминала сохраняются в исходном виде. Текст лицензий оставлен в оригинале.

## Переход с портативного KiTTY

Сначала сделайте копию каталога старого клиента. Разместите KiTTY-RUS в копии рядом с `kitty.ini`, `Sessions` и `SshHostKeys` либо укажите каталог явно:

```powershell
.\kitty-rus.exe --portable-dir "D:\Terminal\KiTTY"
```

Поддерживаются старые файлы сессий `Ключ\значение\`, кодирование `%XX`, вложенные каталоги, расширения файлов из `kitty.ini` и пути из `putty.conf`. Неизвестные поля старого KiTTY сохраняются при перезаписи сессии. В окне сессий доступны выбор папки и поиск по имени.

Сохранённый пароль загружается из старого поля `Password` с учётом `cryptsalt`. В том числе поддерживается вход под `root`, обычная SSH-аутентификация паролем и запрос пароля через keyboard-interactive. Пароль хранится в совместимом обратимом формате KiTTY; это **не защищённое хранилище секретов**. Доступ к файлу сессии позволяет восстановить пароль. Не публикуйте реальные `Sessions`, `kitty.ini` и ключи.

Полностью зашифрованные файлы профилей с мастер-паролем пока не поддерживаются. Если старый пароль не удалось декодировать, исходное значение сохраняется; введите пароль вручную для замены.

## Версии

Числовая версия совпадает с базовой версией PuTTY. Первая тестовая публикация — `0.85-test`, следующая — `0.85-test-2`; после завершения проверки — `0.85`. Отдельный номер версии KiTTY-RUS не добавляется. Git-теги имеют префикс `v`.

## Сборка и проверка

Нужны Visual Studio 2022 с компонентами C++ для нужной архитектуры, Windows SDK, CMake и Python 3. ARM64 требует соответствующих инструментов Visual Studio.

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --parallel
python tools/check-translations.py
```

Для x86 и ARM64 замените `windows-x64` на `windows-x86` или `windows-arm64`. Имя CMake-цели терминала осталось `putty`, результат — `kitty-rus.exe`. Автоматические сборки доступны в Actions.

```powershell
cmake --build build/windows-x64 --config Release --target test_kittyrus test_kittyrus_legacy testcrypt test_host_strfoo test_bpp
python test/run_kittyrus.py build/windows-x64/Release/test_kittyrus.exe
python test/run_kittyrus_legacy.py build/windows-x64/Release/test_kittyrus_legacy.exe
```

Проверка реального SSH-входа на локальном тестовом сервере требует `paramiko` только в окружении тестирования:

```powershell
python -m pip install paramiko
python test/ssh_saved_password.py build/windows-x64/Release/kitty-rus.exe build/windows-x64/Release/plink.exe
```

Проверены работающие сборки x64 и x86. ARM64 собрана и проверена по формату PE; запуск на устройстве ARM64 пока не проверен. Сборки не подписаны Authenticode.

Изменение стадии выпуска: `python tools/set-release-stage.py test-2` или `stable`. После изменения каталога переводов выполните `python tools/generate-translations.py`.

## Происхождение и лицензия

База: [PuTTY 0.85](https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html). Расширения и совместимость: [KiTTY](https://github.com/cyd01/KiTTY), исходный коммит `75fa2abcd220c17249ff7252f8d5224137001f2d`. История KiTTY сохранена в Git.

См. [лицензию PuTTY](LICENCE) и [лицензию KiTTY](LICENCE-KiTTY.txt). Новый значок с двумя соединёнными терминалами: [исходное изображение](icons/kitty-rus.png), [сведения о генерации](icons/kitty-rus-prompt.txt).
