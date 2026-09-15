from pathlib import Path
import struct
from intelhex import IntelHex

Import("env")
framework = Path(env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf52"))
env.Append(LIBPATH=[str(framework / "cores/nRF5/linker")])

def uf2(source, target, env):
    hex_path = Path(str(target[0]))
    data = IntelHex(str(hex_path))
    start, end = data.minaddr(), data.maxaddr() + 1
    if start != 0x27000 or end > 0xED000:
        raise ValueError("Application image overlaps the SoftDevice, filesystem, or bootloader")
    raw = bytes(data.tobinarray(start=start, end=end - 1))
    sp, reset = struct.unpack_from("<II", raw)
    if not (0x20006000 <= sp <= 0x20040000 and start <= (reset & ~1) < end and reset & 1):
        raise ValueError("Invalid application vector table")
    count = (len(raw) + 255) // 256
    blocks = []
    for index in range(count):
        payload = raw[index*256:(index+1)*256].ljust(256, b"\xff")
        header = struct.pack("<8I", 0x0A324655, 0x9E5D5157, 0x2000,
                             start + index*256, 256, index, count, 0xADA52840)
        blocks.append(header + payload + bytes(220) + struct.pack("<I", 0x0AB16F30))
    output = hex_path.with_suffix(".uf2")
    output.write_bytes(b"".join(blocks))
    print(f"Created {output} ({count} application-only UF2 blocks)")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", uf2)
