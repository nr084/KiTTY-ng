# Portable profile integration checks

Date: 2026-10-27.

`test_profiles.c` calls the production `profiles.c` against real synthetic files
and Windows filesystem APIs. No profile parser or PE validator is mocked.
It does not read a user's existing KiTTY-ng profiles.

## Observed result

The x64 executable completed for release 1.0 under Wine 9.0 with exit code 0.
The profile parser and architecture selector are unchanged in 1.0.1; this
individual suite was not rerun for that patch release:

```text
Profiles: 127 checks, 0 failures, 0 skipped capability tests.
```

This includes fixture creation checks and behavior assertions. Both the x86 and
x64 tests compiled with LLVM-MinGW 20261020, C11, `-Wall -Wextra -Wpedantic -Werror`.
Clang static analysis of `profiles.c` also completed without diagnostics.
The x86 executable was not run: this environment provides only Wine64.
Wine execution does not replace validation in actual Windows Total Commander.

## Covered behavior

- Original `Key\\value\\` profile syntax and percent-encoded values.
- Root `Sessions` layout, nested Cyrillic UTF-8 names and nonstandard SSH port.
- Reversible flat labels containing percent signs, separators, quotes,
  Windows-reserved punctuation, control characters and trailing dots/spaces.
- The reserved WFX settings label is escaped for an identically named profile.
- Default Settings, non-SSH, empty-host, invalid-port, malformed/invalid-UTF-8
  names, embedded NUL escapes, encoded slash aliases, encrypted/unrecognized
  profiles and `.tmp` files are skipped.
- Ambiguous case-insensitive display labels are removed instead of selecting
  an arbitrary profile.
- Noncanonical physical directory names containing `%XX` escapes are skipped
  with an explicit warning. A fixture verifies that `Encoded%20Folder/Same`
  cannot shadow the different server stored in `Encoded Folder/Same`.
  Canonical raw Unicode directory names and percent-encoded session filenames
  remain supported.
- Each file is checked against Klink's actual canonical filename, optional
  suffix and raw-name fallback precedence using Windows file identity.
  Noncanonical `%41Only`, an extra suffix and a raw-name SSH profile shadowed
  by a canonical non-SSH profile are rejected. Valid raw-space fallback and
  the canonical profile remain usable. This prevents a displayed profile from
  launching a different saved server or falling back to Default Settings.
- v1.0 retains a valid nested profile whose combined legacy label exceeds 255
  UTF-16 units. The hierarchy enforces limits per displayed component instead.
- Depth greater than 16 and reparse points are skipped, including a directory
  symlink pointing back to the sessions directory.
- `kitty.ini` file suffix and `putty.conf` directory/suffix precedence,
  case-sensitive suffix matching and environment expansion.
- Exact matching helper executable names, real PE architecture matching,
  wrong-machine rejection for both Klink and the GUI client, malformed-PE rejection, small-output-buffer handling
  and rejection of an unsuffixed `klink.exe` fallback.
- Missing portable root reports a failure without retaining partial results.

## Compiled settings-dialog gate

`test_settings_resource.c` was compiled as an x64 Windows executable and run
against both final `dist/kitty-ng-fs.wfx` and `dist/kitty-ng-fs.wfx64` under Wine64.
It loads the DLLs with `LOAD_LIBRARY_AS_DATAFILE`; neither plugin entry point is
executed. It parses the real `DLGTEMPLATEEX` resource 101, rather than reading
the source `.rc` file or searching for strings.

```text
Inspected compiled settings dialog: 13 controls, 1160 resource bytes.
Inspected compiled settings dialog: 13 controls, 1160 resource bytes.
Settings resources: 144 checks, 0 failures; both DLLs loaded as data only.
```

The checks verify the portable-root edit control 1001; the visible, enabled
Browse pushbutton 1002 with its actual Russian caption; the remote-root edit
1003; the numeric timeout edit 1004; and Save/Cancel buttons with the expected
IDs, button classes and styles. The author label 1005 and homepage button
1006 contain the exact requested attribution and URL. The compiled VERSIONINFO
contains CompanyName/Author nr084, the expected Comments/LegalCopyright and
URLInfoAbout, text version 1.0.1 and numeric version 1.0.1.0.
This confirms the controls and metadata exist in both compiled artifacts.
It is not a test of clicking Browse/Homepage, Windows desktop interaction,
or executing the x86 plugin.

Compile and run this focused resource gate with:

```sh
x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Wpedantic -Werror \
  -D_WIN32_WINNT=0x0601 -municode tests/test_settings_resource.c \
  -o test_settings_resource.exe -lversion
test_settings_resource.exe dist/kitty-ng-fs.wfx dist/kitty-ng-fs.wfx64
```

## Compile the test on a Windows-targeting compiler

From the project root, using the corresponding LLVM-MinGW compiler:

```sh
x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Wpedantic -Werror \
  -D_WIN32_WINNT=0x0601 src/profiles.c tests/test_profiles.c \
  -o test_profiles_x64.exe

i686-w64-mingw32-clang -std=c11 -Wall -Wextra -Wpedantic -Werror \
  -D_WIN32_WINNT=0x0601 src/profiles.c tests/test_profiles.c \
  -o test_profiles_x86.exe
```

Run the matching EXE on Windows. Reparse-point capability tests report a skip
when Windows does not permit the test process to create symbolic links.
The test creates and removes an isolated directory under the temporary folder.

## Research and test-runtime provenance

The storage/CLI behavior was inspected in
[`nr084/KiTTY-ng`](https://github.com/nr084/KiTTY-ng), main revision
`f0e383e7f3373126843d942f173689daacdcbf05`, particularly
`windows/kittyng-portable.c`, `windows/kittyng-keypaths.c`, `cmdline.c` and
`windows/plink.c`.
All six relevant files (`kittyng-portable.c`, `kittyng-keypaths.c`, `kittyng.c`,
`plink.c`, `storage.c` and `cmdline.c`) were additionally downloaded at the exact
release-binary revision below and found byte-identical to the inspected main
revision.

The official KiTTY-ng v0.85-6 x64 release archive was downloaded separately for
subsequent end-to-end transport testing. Its SHA-256 is
`2c7d3f96e14f85743e4d170dece2a1f6f546613bedbc8f92fe36636df89cd961`.
Its Klink reports source revision
`b95f861c7425b1d8c493c6bce9ba492d3a87161a` and was successfully launched in Wine.
Those executable files are external prerequisites and are not part of this
plugin's source package.

The temporary Wine runtime used official Ubuntu repository packages extracted
without a system installation:

| Package | SHA-256 |
| --- | --- |
| `wine64_9.0~repack-4build3_amd64.deb` | `7f4f912b21917f28c281c99e3244ee801be2e20f0a23cd9f6d37acc97f5041ff` |
| `libwine_9.0~repack-4build3_amd64.deb` | `eb421a3660bfdc090d70ce16b06aadfde1c2bce2593292023e520988f45d009e` |
| `libz-mingw-w64_1.3.2+dfsg-1_all.deb` | `42a4cceccf19e42f04b6144f5e25c28efab6d95c111723c80fce8dccfcaf717c` |

These dependency/runtime files are not included in the deliverable. An isolated
Wine prefix and relocatable DLL links were used only for local checks.
