"""Publish the verified local Windows and macOS Universal release, preserving previous releases."""
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
args = parser.parse_args()
version = (ROOT / "VERSION").read_text().strip()
tag = "v" + version
report = json.loads((ROOT / "dist/release-build.json").read_text())
commit = git("rev-parse", "HEAD", text=True).strip()
assert not git("status", "--porcelain"), "Commit all changes before publishing"
assert report["status"] == "success" and report["version"] == version
assert report["source_commit"] == commit and not report["working_tree_dirty"]
expected = {f"kitty-ng-{version}-{arch}.zip" for arch in ("x64", "x86", "arm64")} | {f"kitty-ng-{version}-macos-universal.dmg"}
assert {a["file"] for a in report["archives"]} == expected
for asset in report["archives"]:
    assert hashlib.sha256((ROOT / "dist" / asset["file"]).read_bytes()).hexdigest() == asset["sha256"]
credential = subprocess.run(
    ["git", "-c", "safe.directory=" + str(ROOT), "credential", "fill"], cwd=ROOT,
    input="protocol=https\nhost=github.com\npath=" + REPO + "\n\n",
    text=True, capture_output=True, check=True)
values = dict(line.split("=", 1) for line in credential.stdout.splitlines() if "=" in line)
headers = {"Authorization": "Bearer " + values["password"],
           "User-Agent": "KiTTY-NG local release", "Accept": "application/vnd.github+json"}

def request(url, method="GET", data=None, content_type="application/json"):
    if isinstance(data, dict): data = json.dumps(data).encode()
    req = urllib.request.Request(url, data=data, method=method,
                                 headers={**headers, "Content-Type": content_type})
    with urllib.request.urlopen(req, timeout=60) as response:
        body = response.read()
        return json.loads(body) if body else None

releases = request(API + "/releases")
release = next((r for r in releases if r["tag_name"] == tag), None)
assert release is None or release["draft"], "Refusing to replace a published release"
body = (ROOT / "docs/releases" / (version + ".md")).read_text(encoding="utf8")
body += "\n\n### SHA-256\n\n```text\n" + (ROOT / "dist/SHA256SUMS.txt").read_text() + "```\n"
metadata = {"tag_name": tag, "target_commitish": commit, "name": "KiTTY-NG " + version,
            "body": body, "draft": True, "prerelease": "-test" in version}
if release is None:
    release = request(API + "/releases", "POST", metadata)
else:
    release = request(API + "/releases/" + str(release["id"]), "PATCH", metadata)
for asset in release["assets"]:
    request(API + "/releases/assets/" + str(asset["id"]), "DELETE")
for asset in report["archives"]:
    path = ROOT / "dist" / asset["file"]
    url = release["upload_url"].split("{", 1)[0] + "?name=" + urllib.parse.quote(path.name)
    uploaded = request(url, "POST", path.read_bytes(), "application/x-apple-diskimage" if path.suffix == ".dmg" else "application/zip")
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
# Confirm publication and exact assets; previous releases are retained.
published = request(API + "/releases/tags/" + tag)
assert published["id"] == release["id"] and not published["draft"]
assert {a["name"] for a in published["assets"]} == expected
print("Published:", published["html_url"], flush=True)
