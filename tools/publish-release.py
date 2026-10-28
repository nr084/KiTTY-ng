"""Publish the verified local Windows, macOS Universal and Linux release, retaining only the current client release."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
REPO = "nr084/kitty-ng"
API = "https://api.github.com/repos/" + REPO

def git(*args, **kwargs):
    return subprocess.check_output(["git", "-c", "safe.directory=" + str(ROOT), *args],
                                   cwd=ROOT, **kwargs)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--draft-only", action="store_true", help="Upload a reviewable draft without publishing")
parser.add_argument("--pending-macos", action="store_true", help="Upload only a draft with verified Windows/Linux packages while waiting for native Mac artifacts")
parser.add_argument("--update-current", action="store_true", help="Explicitly replace changed assets in the existing stable release without moving its tag")
parser.add_argument("--windows-linux", action="store_true", help="Publish the authorized Windows/Linux stage; clearly mark macOS pending")
args = parser.parse_args()
assert not args.windows_linux or not (args.pending_macos or args.update_current)
assert not args.update_current or not (args.draft_only or args.pending_macos)
assert not args.pending_macos or args.draft_only, "A pending macOS release must remain a draft"
version = (ROOT / "VERSION").read_text().strip()
tag = "v" + version
report = json.loads((ROOT / "dist/release-build.json").read_text())
commit = git("rev-parse", "HEAD", text=True).strip()
assert not git("status", "--porcelain"), "Commit all changes before publishing"
assert report["status"] == ("pending-macos" if args.pending_macos else "success") and report["version"] == version
assert report["source_commit"] == commit and not report["working_tree_dirty"]
expected = {f"kitty-ng-{version}-{arch}.zip" for arch in ("x64", "x86", "arm64")} | {f"kitty-ng-{version}-macos-universal.dmg", f"kitty-ng-{version}-x86_64.AppImage", f"kitty-ng-{version}-x86_64.tar.zst"}
if args.pending_macos or args.windows_linux:
    expected.remove(f"kitty-ng-{version}-macos-universal.dmg")
assert report.get("release_scope", "all") == ("windows-linux" if args.windows_linux else "all")
assert {a["file"] for a in report["archives"]} == expected
for asset in report["archives"]:
    assert hashlib.sha256((ROOT / "dist" / asset["file"]).read_bytes()).hexdigest() == asset["sha256"]
credential = subprocess.run(
    ["git", "-c", "safe.directory=" + str(ROOT), "credential", "fill"], cwd=ROOT,
    input="protocol=https\nhost=github.com\npath=" + REPO + "\n\n",
    text=True, capture_output=True, check=True)
values = dict(line.split("=", 1) for line in credential.stdout.splitlines() if "=" in line)
headers = {"Authorization": "Bearer " + values["password"],
           "User-Agent": "KiTTY-ng local release", "Accept": "application/vnd.github+json"}

def request(url, method="GET", data=None, content_type="application/json"):
    if isinstance(data, dict): data = json.dumps(data).encode()
    req = urllib.request.Request(url, data=data, method=method,
                                 headers={**headers, "Content-Type": content_type})
    with urllib.request.urlopen(req, timeout=60) as response:
        body = response.read()
        return json.loads(body) if body else None

releases = request(API + "/releases")
release = next((r for r in releases if r["tag_name"] == tag), None)
assert release is None or release["draft"] or args.update_current, "Use explicit --update-current to replace a published release"
if args.update_current:
    assert release and not release["draft"] and not release["prerelease"]
    assert request(API + "/releases/latest")["id"] == release["id"]
body = (ROOT / "docs/releases" / (version + ".md")).read_text(encoding="utf8")
if args.windows_linux:
    body = "**Windows/Linux опубликованы. macOS 0.85-9 готовится отдельно и будет добавлена после нативной проверки.**\n\n".replace("0.85-9", version) + body
body += "\n\n### SHA-256\n\n```text\n" + (ROOT / "dist/SHA256SUMS.txt").read_text() + "```\n"
if report.get("platform_commits"):
    body += "\n\n### Исходники пакетов / Package sources\n\n" + "\n".join(
        "- " + platform + ": `" + revision + "`" for platform, revision in report["platform_commits"].items()) + "\n"
metadata = {"tag_name": tag, "target_commitish": commit, "name": "KiTTY-ng " + version,
            "body": body, "draft": True, "prerelease": "-test" in version}
if args.update_current:
    # Stage and verify each replacement before removing its old asset.
    for asset in report["archives"]:
        path = ROOT / "dist" / asset["file"]
        existing = next((a for a in release["assets"] if a["name"] == path.name), None)
        if existing and existing.get("digest") == "sha256:" + asset["sha256"]:
            print("Unchanged:", path.name, flush=True)
            continue
        staged_name = path.name + ".replacement-" + asset["sha256"][:12]
        url = release["upload_url"].split("{", 1)[0] + "?name=" + urllib.parse.quote(staged_name)
        staged = next((a for a in release["assets"] if a["name"] == staged_name), None)
        if not staged:
            staged = request(url, "POST", path.read_bytes(), "application/octet-stream")
        assert staged["size"] == path.stat().st_size and staged.get("digest") == "sha256:" + asset["sha256"]
        if existing:
            request(API + "/releases/assets/" + str(existing["id"]), "DELETE")
        request(API + "/releases/assets/" + str(staged["id"]), "PATCH", {"name": path.name})
        print("Updated:", path.name, flush=True)
    release = request(API + "/releases/" + str(release["id"]), "PATCH", {"body": body})
else:
    if release is None:
        release = request(API + "/releases", "POST", metadata)
    else:
        release = request(API + "/releases/" + str(release["id"]), "PATCH", metadata)
    for asset in release["assets"]:
        request(API + "/releases/assets/" + str(asset["id"]), "DELETE")
    for asset in report["archives"]:
        path = ROOT / "dist" / asset["file"]
        url = release["upload_url"].split("{", 1)[0] + "?name=" + urllib.parse.quote(path.name)
        uploaded = request(url, "POST", path.read_bytes(), {".dmg":"application/x-apple-diskimage", ".zip":"application/zip", ".AppImage":"application/octet-stream", ".zst":"application/zstd"}[path.suffix])
        assert uploaded["size"] == path.stat().st_size
        assert uploaded.get("digest") == "sha256:" + asset["sha256"], "Upload checksum mismatch"
        print("Uploaded:", path.name, flush=True)
release = request(API + "/releases/" + str(release["id"]), "GET")
assert {a["name"] for a in release["assets"]} == expected
if args.draft_only:
    print("Draft prepared:", release["html_url"], flush=True)
    raise SystemExit(0)
publication = {"draft": False}
if not metadata["prerelease"]:
    publication["make_latest"] = "true"
release = request(API + "/releases/" + str(release["id"]), "PATCH", publication)
assert not release["draft"]
# Confirm publication before removing superseded client releases.
published = request(API + "/releases/tags/" + tag)
assert published["id"] == release["id"] and not published["draft"]
assert {a["name"] for a in published["assets"]} == expected
print("Published:", published["html_url"], flush=True)

# The independent plugin has its own release cycle and is never removed here.
import re
assert request(API + "/releases/latest")["id"] == published["id"]
old_client_releases = []
page = 1
while True:
    batch = request(API + f"/releases?per_page=100&page={page}")
    old_client_releases.extend(r for r in batch if r["id"] != published["id"]
                               and not r["draft"]
                               and re.fullmatch(r"v[0-9]+\.[0-9]+-[0-9]+", r["tag_name"]))
    if len(batch) < 100:
        break
    page += 1
for old in old_client_releases:
    request(API + "/releases/" + str(old["id"]), "DELETE")
    print("Removed superseded client release:", old["tag_name"], flush=True)
