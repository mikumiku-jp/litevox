#!/usr/bin/env python3

import hashlib
import json
import os
import sys
from pathlib import Path


EXCLUDED_FILENAMES = {"bundle-manifest.json", "SHA256SUMS"}


def compute_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def collect_entries(bundle_dir: Path) -> list[dict[str, object]]:
    entries: list[dict[str, object]] = []
    for root, _, filenames in os.walk(bundle_dir):
        root_path = Path(root)
        for filename in sorted(filenames):
            if filename in EXCLUDED_FILENAMES:
                continue
            file_path = root_path / filename
            relative_path = file_path.relative_to(bundle_dir).as_posix()
            entries.append(
                {
                    "path": relative_path,
                    "size_bytes": file_path.stat().st_size,
                    "sha256": compute_sha256(file_path),
                }
            )
    entries.sort(key=lambda entry: str(entry["path"]))
    return entries


def write_manifest(bundle_dir: Path, entries: list[dict[str, object]]) -> None:
    manifest_path = bundle_dir / "bundle-manifest.json"
    manifest = {
        "bundle_name": bundle_dir.name,
        "file_count": len(entries),
        "files": entries,
    }
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def write_sha256sums(bundle_dir: Path, entries: list[dict[str, object]]) -> None:
    sha256sums_path = bundle_dir / "SHA256SUMS"
    lines = [f"{entry['sha256']}  {entry['path']}" for entry in entries]
    sha256sums_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: write_bundle_manifest.py BUNDLE_DIR", file=sys.stderr)
        return 1
    bundle_dir = Path(sys.argv[1]).resolve()
    if not bundle_dir.is_dir():
        print(f"bundle directory not found: {bundle_dir}", file=sys.stderr)
        return 1
    entries = collect_entries(bundle_dir)
    write_manifest(bundle_dir, entries)
    write_sha256sums(bundle_dir, entries)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
