"""Build a local Universal 2 AppKit client and portable helpers on Windows.

Cross-compilation and signatures are checked here; native macOS execution
needs independent testing. Never publishes or increments a release.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import plistlib
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
NAMES = ("kitty-ng", "klink", "kscp", "ksftp", "kittygen", "kageant")

def run(args):
    process = subprocess.run([str(a) for a in args], cwd=ROOT)
    if process.returncode:
        raise RuntimeError("Command failed: " + str(args[0]))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deployment-target", default="13.0")
    parser.add_argument("--offline", action="store_true", help="Reuse installed tools without checking new stable releases")
    parser.add_argument("--output", type=Path, default=ROOT/"dist")
    args = parser.parse_args()
    os.environ.update(PYTHONUTF8="1", PYTHONDONTWRITEBYTECODE="1")
    if not args.offline:
        for installer in ("fetch-macos-llvm.py", "fetch-macos-sdk.py", "fetch-macos-host-tools.py", "fetch-macos-aux-tools.py"):
            run([sys.executable, ROOT/"tools"/installer])
    store = ROOT/"_tools/macos"
    sdk_info = json.loads((store/"sdk/sdk-info.json").read_text())
    candidates = sorted((store/"llvm").glob("*/toolchain-info.json"), key=lambda p:tuple(map(int,p.parent.name.split("."))))
    tool_info = json.loads(candidates[-1].read_text()); tools = candidates[-1].parent/"bin"
    signer = next((store/"rcodesign").rglob("rcodesign.exe"))
    version = (ROOT/"VERSION").read_text().strip()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    report = dict(source_commit=subprocess.check_output(["git","-c","safe.directory="+str(ROOT),"rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
                  working_tree_dirty=bool(subprocess.check_output(["git","-c","safe.directory="+str(ROOT),"status","--porcelain"],cwd=ROOT)),version=version, stage="native-terminal", runtime_verified=False,
                  architectures=["arm64", "x86_64"], deployment_target=args.deployment_target,
                  sdk=sdk_info, llvm=tool_info,
                  signer=subprocess.check_output([str(signer),"--version"],text=True).strip())
    (ROOT/"build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="macos-build-", dir=ROOT/"build") as temporary:
        work = Path(temporary); app = work/"KiTTY-ng.app"
        macos = app/"Contents/MacOS"; helpers = app/"Contents/Helpers"; resources = app/"Contents/Resources"
        for directory in (macos,helpers,resources):directory.mkdir(parents=True)
        native = output/"macos-core"
        run([sys.executable,ROOT/"tools/macos-core-build.py","--output",native,
             "--deployment-target",args.deployment_target])
        libraries = {}; executables = []
        for name in NAMES:
            executable = (macos if name=="kitty-ng" else helpers)/name; executables.append(executable)
            run([tools/"llvm-lipo.exe","-create",*[native/arch/name for arch in report["architectures"]],"-output",executable])
            run([tools/"llvm-lipo.exe",executable,"-verify_arch","arm64","x86_64"])
            headers = subprocess.check_output([str(tools/"llvm-objdump.exe"),"--macho","--private-headers","--arch=all",str(executable)],text=True,encoding="utf8")
            libs = subprocess.check_output([str(tools/"llvm-objdump.exe"),"--macho","--dylibs-used","--arch=all",str(executable)],text=True,encoding="utf8")
            for line in libs.splitlines():
                line=line.strip()
                if line.startswith("/") and not line.startswith(("/usr/lib/","/System/Library/")):
                    raise RuntimeError("Non-system runtime dependency: "+line)
            libraries[name]=libs
        info=dict(CFBundleName="KiTTY-ng",CFBundleDisplayName="KiTTY-ng",CFBundleExecutable="kitty-ng",
                  CFBundleIdentifier="com.nr084.kitty-ng",CFBundlePackageType="APPL",
                  CFBundleShortVersionString=version.split("-")[0],CFBundleVersion=version.replace("-","."),
                  LSMinimumSystemVersion=args.deployment_target,NSHighResolutionCapable=True,CFBundleIconFile="kitty-ng.icns")
        (app/"Contents/Info.plist").write_bytes(plistlib.dumps(info))
        from PIL import Image
        Image.open(ROOT/"icons/kitty-ng.png").save(resources/"kitty-ng.icns",format="ICNS")
        for license in ("LICENCE","LICENCE-KiTTY.txt"):
            (resources/license).write_bytes((ROOT/license).read_bytes())
        (resources/"BUILD-INFO.json").write_text(json.dumps(report,indent=2)+"\n")
        # Explicit empty config prevents accidental use of a user's signing identity.
        config=work/"signing.toml";config.write_text("")
        run([signer,"--config-file",config,"sign","--timestamp-url","none",app])
        spec=importlib.util.spec_from_file_location("verify_macos",ROOT/"tools/verify-macos.py")
        validator=importlib.util.module_from_spec(spec);spec.loader.exec_module(validator)
        validator.verify_bundle(app)
        for executable in executables:validator.verify_binary(executable,app if executable.parent==macos else None)
        # Real negative checks: modified code/resources must not pass validation.
        executable=macos/"kitty-ng";original=executable.read_bytes()
        cpu,thin=next(validator.slices(original));offset=original.find(thin)
        changed=bytearray(original);changed[offset+4096]^=1;executable.write_bytes(changed)
        try:
            try:validator.verify_binary(executable,app)
            except (AssertionError,ValueError,IndexError,KeyError):pass
            else:raise RuntimeError("Tampered code was accepted")
        finally:executable.write_bytes(original)
        resource=resources/"BUILD-INFO.json";original=resource.read_bytes();resource.write_bytes(original+b" ")
        try:
            try:validator.verify_bundle(app)
            except AssertionError:pass
            else:raise RuntimeError("Tampered resource was accepted")
        finally:resource.write_bytes(original)
        readme = work/"README-macos.txt"
        readme.write_text("KiTTY-ng — версия macOS Universal 2 (ARM64 + Intel).\n"
            "Распакуйте KiTTY-ng.app рядом с Windows EXE, kitty.ini и Sessions.\n"
            "Редактор профилей: Новый / Изменить / Удалить / Дублировать; остальные параметры — дерево Все настройки.\n"
            "Передача файлов: перетащите файл в терминал либо выберите отправку/скачивание в контекстном меню.\n"
            "kscp получает текущие настройки SSH через приватный канал, включая пароль, ключ, сертификат и прокси.\n"
            "Автокоманды, UTF-8 скрипты, переподключение и оформление задаются в редакторе.\n"
            "KiTTYgen и Kageant доступны в меню; агент — также в строке меню macOS.\n"
            "Язык English / Русский сохраняется кнопкой Применить; утилиты используют тот же выбор.\n"
            "Основной SSH-терминал проверен пользователем на M3; новые функции требуют проверки на Mac.\n"
            "Добавлены SSH CA, системный GSSAPI, сочетания из kitty.ini, дерево всех сохраняемых настроек, CP1251 имена и расширенные шрифты. Нативная проверка этих функций ещё требуется; устаревшие исключённые расширения и печать не перенесены.\n"
            "Консольные Universal-утилиты находятся в Contents/Helpers.\n"
            "Нет Developer ID/нотариального заверения; используется локальная ad-hoc подпись.\n"
            "Если macOS блокирует запуск: Настройки системы → Конфиденциальность и безопасность → Всё равно открыть.\n"
            "При AppTranslocation выберите общую портативную папку вручную.\n"
            "Smoke-test KiTTY-ng.command проверяет утилиты на синтетических ключах в очищаемом временном каталоге.\n"
            "Для диагностики падения: Diagnose KiTTY-ng.command; журнал kitty-ng-diagnostic.log.\n"
            "Based on PuTTY by Simon Tatham and contributors, and KiTTY by Cyril Dupont.\n",encoding="utf8")
        diagnostic=work/"Diagnose KiTTY-ng.command"
        diagnostic.write_bytes((ROOT/"macos/Diagnose KiTTY-ng.command").read_bytes().replace(b"\r\n",b"\n"))
        smoke=work/"Smoke-test KiTTY-ng.command"
        smoke.write_bytes((ROOT/"macos/Smoke-test KiTTY-ng.command").read_bytes().replace(b"\r\n",b"\n"))
        archive=output/f"kitty-ng-{version}-macos-universal.zip"
        with tempfile.NamedTemporaryFile(dir=output,suffix=".zip",delete=False) as pending:pending_path=Path(pending.name)
        try:
            with zipfile.ZipFile(pending_path,"w",zipfile.ZIP_DEFLATED,compresslevel=9) as bundle:
                for path in sorted(app.rglob("*"))+[readme,diagnostic,smoke]:
                    if not path.is_file():continue
                    entry=zipfile.ZipInfo(path.relative_to(work).as_posix());entry.create_system=3
                    entry.external_attr=(0o100755 if path in executables or path in (diagnostic,smoke) else 0o100644)<<16
                    entry.compress_type=zipfile.ZIP_DEFLATED;bundle.writestr(entry,path.read_bytes())
            pending_path.replace(archive)
        finally:pending_path.unlink(missing_ok=True)
        report.update(archive=archive.name,sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
                      libraries=libraries,status="success",executables=list(NAMES),tamper_checks="passed")
        (output/"macos-build.json").write_text(json.dumps(report,indent=2)+"\n")
        import shutil
        if native.resolve().is_relative_to(output):shutil.rmtree(native)
        print("PASS: native terminal and five Universal 2 helpers, ad-hoc seals and tamper checks; "+str(archive),flush=True)
if __name__=="__main__":main()
