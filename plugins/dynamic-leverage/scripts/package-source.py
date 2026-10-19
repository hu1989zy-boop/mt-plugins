#!/usr/bin/env python3
"""Package this plugin only: no licensed SDK, build products or runtime data."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ALLOWED = {"shared", "mt4", "mt5", "management", "config", "docs", "scripts"}
SUFFIXES = {".cpp", ".hpp", ".h", ".def", ".txt", ".md", ".ps1", ".cmd", ".py", ".html", ".css", ".mjs", ".json", ".ini", ".cmake"}


def source_files():
    for path in sorted(ROOT.rglob("*")):
        rel = path.relative_to(ROOT)
        if path.is_symlink() or not path.is_file() or ".local." in path.name:
            continue
        # Filter build directories, not source filenames such as build-windows.ps1.
        if any(part in {"build", "out", "runtime", "vendor", "__pycache__"} or part.startswith("build-") for part in rel.parts[:-1]):
            continue
        if len(rel.parts) == 1:
            if rel.name not in {"CMakeLists.txt", "README.md", ".gitignore"}:
                continue
        elif rel.parts[0] not in ALLOWED or path.suffix not in SUFFIXES:
            continue
        yield path, rel


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "build/release/dynamic-leverage-opening-margin-validation-source.zip")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    manifest = {"plugin":"dynamic-leverage", "version":"0.2.0", "kind":"source",
                "created_at_utc":datetime.now(timezone.utc).isoformat(), "windows_dll_included":False,
                "stage":"opening_margin_validation", "native_execution_available":False,
                "execution_policy":"new_positions_only_post_open_volume",
                "required_sdk":{"mt4":"Server API 5, Win32", "mt5":"Server API 6182, x64"}, "files":[]}
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path, rel in source_files():
            name = "plugins/dynamic-leverage/" + rel.as_posix()
            content = path.read_bytes()
            archive.writestr(name, content)
            manifest["files"].append({"path":name,"sha256":hashlib.sha256(content).hexdigest()})
        archive.writestr("source-manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    with zipfile.ZipFile(args.output) as archive:
        if archive.testzip() is not None:
            raise RuntimeError("Source archive CRC check failed")
        for item in manifest["files"]:
            if hashlib.sha256(archive.read(item["path"])).hexdigest() != item["sha256"]:
                raise RuntimeError("Source archive content verification failed")
    print(f"Source package verified: {args.output.resolve()} ({len(manifest['files'])} files)")


if __name__ == "__main__":
    main()
