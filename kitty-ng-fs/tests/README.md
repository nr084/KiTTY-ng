# Регрессии больших загрузок (1.1.1)

`test_large.py` вызывает настоящий WFX x64 через ctypes и поднимает синтетический SFTP-сервер на loopback. Требуется 64-битный Python с Paramiko. `--mib 8193` проверяет файл больше 8 ГБ, шаблон включает номер каждого блока; временная копия удаляется после проверки. Нужны примерно 9 ГБ свободного места. `--latency-ms 20` задерживает ответы без сериализации очереди запросов. `--cancel-mib 4` проверяет отмену и сохранность прежнего файла.

Пример: `python tests/test_large.py --dll dist/development/kitty-ng-fs.wfx64 --mib 8193 --report dist/development/large.json`.

`test_sftp.c` дополнен пакетными READ: перестановка и дубли ID, EOF, короткий ответ, ошибка сервера, смещения больше 4 ГБ и оборот счётчика ID. `test_wfx.c` проверяет медленный callback прогресса и отмену до финального commit файла.

---

# Текущая рабочая ветка: SSH внутри WFX

Сборка, нативный WFX-тест с loopback SFTP и сценарии аутентификации описаны в [EMBEDDED-SSH.md](../EMBEDDED-SSH.md). `test_embedded.py` проверяет обе стороны передачи, операции и отмену через настоящий DLL, а `test_auth.c` — доверие серверному ключу, пароль, PPK и отсутствие изменений окружения/CWD. Paramiko нужен только тестам, не плагину. Публичный релиз остаётся 1.0.3; записи ниже исторические.

---

# Изменения проверки 1.0.3

Команда `python build-local.py --toolchain C:/tools/llvm-mingw-20261020 --tests session_tree icons` собирает обе DLL и проверяет изменённое дерево, реальный WFX-интерфейс и ресурсы. Добавлены имена с точками, буквальным U+00B7, процентами, вложенные и плоские старые пути. Актуальный отчёт — [PUBLICATION.md](../PUBLICATION.md). Ниже сохранены команды и результаты предыдущих выпусков.

# KiTTY-ng FS 1.0.2 verification

Run `python build-local.py --toolchain C:/tools/llvm-mingw-20261020` from the plugin root for the current native Windows build/test workflow. Both architectures are built and executed. The new wide-tree regression checks 10,000 mixed siblings, exact identity, misses and case mismatch. Current results and limitations are in [PUBLICATION.md](../PUBLICATION.md).

## Historical recipes and results

# KiTTY-ng FS 1.0.2 verification

The protocol, profile and path fixtures use synthetic data and temporary files.
They do not load the user's KiTTY-ng data. The explicitly configured WFX
integration fixture connects to a dedicated test SSH server, as described below.

Run the build commands below from the source project root. They are recipes for
repeating the tests; retaining a recipe does not claim it was rerun for 1.0.2.
The historical protocol/OpenSSH/path checks remain available here. The current
release execution record and its platform limitations are in `VALIDATION.md`.

## Portable C protocol tests

From the project root, with GCC or Clang on a POSIX host:

~~~sh
mkdir -p build
cc -std=c99 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -g \
  src/sftp.c tests/test_sftp.c -o build/test_sftp
./build/test_sftp
~~~

Expected result: 19 groups, including 2,000 deterministic malformed replies.
Coverage includes independently checked request bytes, binary handles and
content, 64-bit offsets, Unicode filename bytes, attribute parsing, status
handling, bounds, truncated replies, request identifiers and safe replacement.
The tests verify that an unsupported atomic replacement sends no destructive
fallback operation.

Some restricted containers block LeakSanitizer's access to /proc.
In that environment, run the already-built binary with:

~~~sh
ASAN_OPTIONS=detect_leaks=0 ./build/test_sftp
~~~

AddressSanitizer and UndefinedBehaviorSanitizer remain enabled; leak detection
is not claimed in that run.

The same sources can be compiled with a Windows C compiler without sanitizer
flags. They use only standard C99 and synthetic memory transports.

## Interoperability with an actual OpenSSH server

On Linux, build a native shared library and run:

~~~sh
cc -std=c99 -Wall -Wextra -Werror -pedantic -fPIC -shared -O2 \
  src/sftp.c -o build/libsftp-test.so
python3 tests/integration_sftp.py --library build/libsftp-test.so
~~~

The script locates a local sftp-server executable in common OpenSSH locations.
If necessary, supply --server /absolute/path/to/sftp-server.
On macOS, build with -dynamiclib instead of -shared, use a .dylib filename
and pass that filename to --library.

The shared library must be native to the host running Python and have the same
bitness as Python. A Windows .wfx file is not an input to this POSIX fixture.
The script needs only Python's standard library; it exchanges SFTP packets
through subprocess pipes. It does not start sshd, open a network listener,
use credentials or change SSH configuration.

The interoperability check covers:

- More than 2 MiB of byte-exact binary upload and download.
- Cyrillic, spaces, quotes, dollar signs and ampersands in filenames.
- Directory creation/listing/deletion, missing-file errors and continued use
  of a connection after an ordinary server error.
- Stat versus lstat for symbolic links.
- Permission changes and a timestamp after the signed 32-bit 2038 boundary.
- Non-overwriting rename preserving an existing destination, followed by
  advertised OpenSSH atomic replacement.
- Sparse file offsets above 5 GiB, without allocating a 5 GiB data buffer.

All filesystem changes are confined to a new temporary directory which is
removed when the test ends. The sparse-file test requires a filesystem that
supports sparse files; its logical size exceeds 5 GiB, while ordinary allocated
data consists of the approximately 2 MiB transfer fixture and small metadata.

## Windows-specific fixtures

test_profiles.c exercises portable profile parsing and executable selection
against a temporary synthetic KiTTY-ng directory. Compile it with profiles.c
and the Windows libraries required by that source. It intentionally excludes
real session credentials. Windows file-symlink checks report a skip on hosts
that do not support creating Windows symbolic links.

test_pathutil.c exercises the actual path and Windows command-line helpers:
literal percent filenames, Unicode, canonical reserved-name aliases, hostile
path components, Windows device namespace rejection, long basenames, bounded
quoting checked by CommandLineToArgvW, cryptographic temporary-name suffixes,
and time conversion through the 2038 and 2106 boundaries.

For example, with a MinGW-w64 toolchain:

~~~sh
x86_64-w64-mingw32-gcc -std=c99 -Wall -Wextra -Werror \
  src/pathutil.c tests/test_pathutil.c \
  -o build/test_pathutil-x64.exe -lbcrypt -lshell32
i686-w64-mingw32-gcc -std=c99 -Wall -Wextra -Werror \
  src/pathutil.c tests/test_pathutil.c \
  -o build/test_pathutil-x86.exe -lbcrypt -lshell32
~~~

Run the resulting executable on a compatible Windows host. Wine can provide
an additional compatibility check, but that does not constitute testing in
Total Commander on Windows.

## Hierarchical session model

`test_session_tree.c` links the actual `src/session_tree.c` module. Its synthetic
profile array supplies only logical names and metadata; there is no replacement
tree parser, plugin host or network transport. The test exercises nested groups,
mixed root groups and sessions, Unicode and escaped components, deterministic
group/session name collisions, stable sorting after reversing discovery order,
direct remote-path resolution and old flat-bookmark aliases. It also checks
explicit path-limit failures and a 10,000-profile tree.

~~~sh
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
  src/session_tree.c tests/test_session_tree.c -o build/test-session-tree-x64.exe
i686-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
  src/session_tree.c tests/test_session_tree.c -o build/test-session-tree-x86.exe
~~~

Run the appropriate executable on a matching Windows environment. The 1.0 x64
test executed under Wine64 and reported **582 checks, 0 failures**. Both test
architectures compiled with warnings treated as errors; x86 execution is not
claimed.

## Built icons, session shortcuts and offline group navigation

`test_icons.c` loads a copy of the matching compiled WFX through its actual
exports. It creates an isolated portable-data fixture with nested groups and
a synthetic Klink launch detector. No real Klink executable, SSH server or
credentials are used. Browsing groups and requesting icons must leave both
the connection callback count and the accidental-launch marker unchanged.
Groups must carry `FILE_ATTRIBUTE_DIRECTORY`; SSH shortcuts must carry
`FILE_ATTRIBUTE_NORMAL`. The synthetic `AServer` sorts alphabetically before
the `Office` group, so the test requires different attributes rather than
relying on the discovery order to put groups first in Total Commander.

The test calls both `FsExecuteFileW` and `FsExecuteFile` with `open` and verifies
the `FS_EXEC_SYMLINK` result and canonical target with a trailing backslash.
Root, nested and old flat shortcut names must redirect without starting SSH.
It accepts a 259-character redirect target and rejects one character more
without changing the caller's buffer. Extended local paths keep that synthetic
fixture independent of the filesystem's ordinary `MAX_PATH` limit. Attempts
to download, delete or rename the shortcut itself must fail without launching
Klink or removing the profile entry.

The check compares the plugin root icon with the DLL's first icon group,
verifies different root/SSH/settings artwork, checks 16- and 32-pixel extraction,
ANSI/Unicode icon callbacks and the background-icon flag. Groups and ordinary
remote paths must return the host's default icon. Large and small
`ExtractIconExW` calls are deliberately independent so each reads its own
embedded frame rather than relying on Wine's paired-size rescaling behavior.

~~~sh
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -municode \
  tests/test_icons.c -o build/test-icons-x64.exe -lshell32 -lgdi32 -luser32
i686-w64-mingw32-gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -municode \
  tests/test_icons.c -o build/test-icons-x86.exe -lshell32 -lgdi32 -luser32
~~~

For example, in PowerShell on a matching Windows host:

~~~powershell
.\build\test-icons-x64.exe 'C:\test\plugin\kitty-ng-fs.wfx64'
~~~

The 1.0.2 x64 test under Wine64 reported **178 checks, 0 failures**. It tests
actual icon handles/resources and offline WFX calls, not interactive Total
Commander rendering. Use the x86 test with `kitty-ng-fs.wfx` in an x86 process;
the x64 test cannot execute that DLL.

## Built settings dialog and version resources

`test_settings_resource.c` loads both compiled plugin architectures using
`LOAD_LIBRARY_AS_DATAFILE` and parses the actual `DLGTEMPLATEEX` resource 101.
It checks the folder edit, visible/enabled Browse pushbutton, remote root,
numeric timeout, default Save and Cancel. The author's visible static text must
be exactly `Автор: nr084`; the enabled homepage button must contain exactly
`Домашняя страница: https://github.com/nr084/KiTTY-ng` and support tab navigation.
The actual VERSIONINFO must identify `nr084` through CompanyName, Author,
Comments and LegalCopyright, and expose the homepage through Comments and
URLInfoAbout. Both numeric file/product versions must be 1.0.2.0, with 1.0.2
in the corresponding string fields.

Reading an x86 resource in the x64 test process does not execute x86 code.
These checks inspect the compiled controls and metadata; clicking the homepage
button and opening a browser remain manual GUI checks.

~~~sh
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -municode \
  tests/test_settings_resource.c -o build/test-settings.exe -lversion
~~~

Run with the two plugin filenames as arguments, for example in PowerShell:

~~~powershell
.\build\test-settings.exe 'C:\test\plugin\kitty-ng-fs.wfx' 'C:\test\plugin\kitty-ng-fs.wfx64'
~~~

The final 1.0.2 resource check passed **144 assertions, 0 failures**, and found
13 controls in each **1160-byte** dialog.

## Actual WFX integration

`test_wfx.c` loads the compiled WFX DLL with `LoadLibraryW` and calls its real
exported ABI. It does not link or mock plugin implementation functions.
It verifies listing, file operations, exact binary content, overwrite refusal,
Unicode/literal percent names, long local names, Unix metadata, cancellation,
MOVE semantics, connection reuse and unload with a live SSH subprocess.

Build the harness with the same architecture as the plugin:

~~~sh
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -municode \
  tests/test_wfx.c -o build/wfx-tests.exe
~~~

Prepare an isolated copy of KiTTY-ng with a working noninteractive session
inside the groups `Lab/Nested`, named `TestServer`, a known verified host key
and access to a dedicated disposable server directory. The corresponding
visible WFX session path is `Lab\Nested\TestServer`; use backslashes in the
harness argument, without a leading or trailing separator. Put a copy of the
WFX DLL beside a `kittyngfs.ini` pointing
to that temporary KiTTY-ng root, and set `RemoteRoot` to the disposable server
directory. The harness creates a unique subdirectory under that root.
Use only test credentials and data. Do not point it at production profiles.

~~~powershell
.\build\wfx-tests.exe 'C:\test\plugin\kitty-ng-fs.wfx64' 'C:\test\local' 'Lab\Nested\TestServer' 'Lab%2FNested%2FTestServer'
~~~

The final optional argument is the old flat bookmark label. Single-quoted
PowerShell arguments preserve its literal percent signs. The harness verifies
that traversing directory groups and opening the SSH shortcut stay offline.
It follows the `FS_EXEC_SYMLINK` target to start the real SSH connection. The
old flat alias must redirect to that same target before connecting, then reuse
the existing connection when listed afterward. It also rejects rename to
another session under the same parent group before opening that session.
Omitting the legacy-label argument omits both compatibility assertions.

The 1.0.2 nested-session test used actual KiTTY-ng 0.85-6 Klink under Wine64,
a localhost SSH server with a synthetic password and pinned RSA host key,
and the real OpenSSH SFTP subsystem. The TCP port differed from 22.
The full result with the legacy-label argument was **61/61 checks passed**.

Protocol/WFX harness interoperability does not replace a manual Total Commander
test. Native Windows dialogs, auto-install interaction, pane refresh and each
user's particular key/agent/proxy authentication still require target testing.
