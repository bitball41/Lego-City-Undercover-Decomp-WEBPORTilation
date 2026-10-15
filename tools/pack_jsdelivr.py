#!/usr/bin/env python3
"""Split the wasm build into jsDelivr-sized chunks and write web/dist/manifest.json."""

import argparse
import hashlib
import json
import shutil
from pathlib import Path

CHUNK_BYTES = 16384


def split_bytes(data, prefix, out_dir):
    names = []
    chunk_dir = out_dir / "chunks"
    chunk_dir.mkdir(parents=True, exist_ok=True)
    for index, start in enumerate(range(0, len(data), CHUNK_BYTES)):
        name = f"chunks/{prefix}-{index:03d}.bin"
        (out_dir / name).write_bytes(data[start : start + CHUNK_BYTES])
        names.append(name)
    return names


def part(name, data, prefix, out_dir):
    return {
        "name": name,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "chunks": split_bytes(data, prefix, out_dir),
    }


def reassemble(manifest, out_dir):
    for item in manifest["parts"]:
        blob = bytearray()
        for name in item["chunks"]:
            blob.extend((out_dir / name).read_bytes())
        if len(blob) != item["size"]:
            raise SystemExit(f"{item['name']} size {len(blob)} != {item['size']}")
        digest = hashlib.sha256(blob).hexdigest()
        if digest != item["sha256"]:
            raise SystemExit(f"{item['name']} hash mismatch")
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--js", required=True)
    parser.add_argument("--wasm", required=True)
    parser.add_argument("--out", default="web/dist")
    args = parser.parse_args()
    out_dir = Path(args.out)
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)
    runtime = Path(args.js).read_bytes()
    wasm = Path(args.wasm).read_bytes()
    manifest = {
        "chunkBytes": CHUNK_BYTES,
        "parts": [
            part("runtime", runtime, "runtime", out_dir),
            part("wasm", wasm, "wasm", out_dir),
        ],
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    reassemble(manifest, out_dir)
    chunks = sum(len(item["chunks"]) for item in manifest["parts"])
    print(f"packed {chunks} chunks into {out_dir}")


if __name__ == "__main__":
    main()
