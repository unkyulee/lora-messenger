"""Verify build artifacts and collect firmware for local hardware testing.

Run after all three PlatformIO builds: python scripts/package.py
This script never accesses a serial port or flashes a device.
"""
from pathlib import Path
import hashlib
import json
import shutil
import struct

ROOT = Path(__file__).resolve().parents[1]

def verify_uf2(path):
    raw = path.read_bytes()
    if not raw or len(raw) % 512:
        raise ValueError("UF2 length is not a positive multiple of 512")
    count = len(raw) // 512
    payload = bytearray()
    for index in range(count):
        block = raw[index*512:(index+1)*512]
        first, second, flags, address, size, number, total, family = struct.unpack_from("<8I", block)
        if (first, second, flags, size, number, total, family) != (
            0x0A324655, 0x9E5D5157, 0x2000, 256, index, count, 0xADA52840
        ):
            raise ValueError(f"Invalid UF2 header at block {index}")
        if address != 0x27000 + index*256 or address + size > 0xED000:
            raise ValueError("UF2 writes outside the application area")
        if struct.unpack_from("<I", block, 508)[0] != 0x0AB16F30:
            raise ValueError("Invalid UF2 end marker")
        payload.extend(block[32:32+size])
    sp, reset = struct.unpack_from("<II", payload)
    if not (0x20006000 <= sp <= 0x20040000 and 0x27000 <= (reset & ~1) < 0x27000+len(payload) and reset & 1):
        raise ValueError("Invalid Wio application vector table")
    return {"blocks": count, "application_address": "0x27000", "family": "0xADA52840"}

def verify_pager(directory):
    factory = (directory / "firmware.factory.bin").read_bytes()
    app = (directory / "firmware.bin").read_bytes()
    boot = (directory / "bootloader.bin").read_bytes()
    partitions = (directory / "partitions.bin").read_bytes()
    if not app or app[0] != 0xE9 or boot[0] != 0xE9:
        raise ValueError("Invalid ESP32 image header")
    if factory[:len(boot)] != boot or factory[0x8000:0x8000+len(partitions)] != partitions:
        raise ValueError("Factory image bootloader/partitions do not match the build")
    if factory[0x10000:0x10000+len(app)] != app:
        raise ValueError("Factory image application does not match the build")
    found = False
    for offset in range(0, len(partitions), 32):
        record = partitions[offset:offset+32]
        if len(record) != 32 or record[:2] != b"\xaa\x50":
            break
        _, kind, subtype, address, size = struct.unpack_from("<HBBII", record)
        if kind == 0 and subtype == 0x10:
            if address != 0x10000 or len(app) > size:
                raise ValueError("Application does not fit app0")
            found = True
    if not found:
        raise ValueError("No app0 partition found")
    return {"flash_address": "0x0", "application_address": "0x10000"}

def main():
    builds = ROOT / ".pio/build"
    specifications = [
        ("wio_l1_oled", "firmware.uf2", "messenger-wio-l1-oled-italy.uf2"),
        ("pager_sx1262", "firmware.factory.bin", "messenger-pager-sx1262-italy.factory.bin"),
        ("pager_lr1121", "firmware.factory.bin", "messenger-pager-lr1121-italy.factory.bin"),
    ]
    # Validate every input before publishing any artifacts into dist.
    checked = []
    for target, source, output in specifications:
        directory = builds / target
        detail = verify_uf2(directory/source) if source.endswith("uf2") else verify_pager(directory)
        checked.append((directory/source, output, target, detail))
    destination = ROOT / "dist"
    destination.mkdir(exist_ok=True)
    manifest = {"status": "hardware-test build; not flashed or radio-tested", "files": []}
    for source, name, target, detail in checked:
        shutil.copyfile(source, destination/name)
        data = (destination/name).read_bytes()
        manifest["files"].append({"name": name, "target": target, "bytes": len(data),
                                  "sha256": hashlib.sha256(data).hexdigest(), **detail})
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))

if __name__ == "__main__":
    main()
