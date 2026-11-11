"""Create small ZIP/ZIP64 fixtures; never reads or copies installed game data."""
import io
import pathlib
import struct
import sys
import warnings
import zipfile
import zlib

root = pathlib.Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
pack = struct.pack

def standard():
    output = io.BytesIO()
    with warnings.catch_warnings(), zipfile.ZipFile(output, "w") as archive:
        warnings.simplefilter("ignore", UserWarning)
        archive.comment = b"archive comment"
        for name, content, method in [("empty", b"", 0), ("dir/", b"", 0), ("duplicate", b"first", 0),
                                      ("duplicate", b"second", 8), ("Unicode/\u00e9.txt", b"unicode", 8),
                                      ("long/" + "x" * 255, bytes(range(256)) * 8, 8)]:
            info = zipfile.ZipInfo(name, date_time=(2024, 2, 29, 12, 34, 56))
            info.comment = b"entry comment"
            info.extra = pack("<HH4s", 0xCAFE, 4, b"test")
            info.compress_type = method
            archive.writestr(info, content)
    return output.getvalue()

def handbuilt(zip64=False, descriptor=False):
    name, content = b"fixture.txt", b"bounded minizip reads\n" * 200
    compressor = zlib.compressobj(6, zlib.DEFLATED, -15)
    data = compressor.compress(content) + compressor.flush()
    crc, flags = zlib.crc32(content), 8 if descriptor else 0
    version = 45 if zip64 else 20
    extra = pack("<HHQQ", 1, 16, len(content), len(data)) if zip64 else b""
    size_c = 0xFFFFFFFF if zip64 else (0 if descriptor else len(data))
    size_u = 0xFFFFFFFF if zip64 else (0 if descriptor else len(content))
    local = pack("<IHHHHHIIIHH", 0x04034B50, version, flags, 8, 0, 0, 0 if descriptor else crc,
                 size_c, size_u, len(name), len(extra)) + name + extra + data
    if descriptor:
        local += pack("<IIII", 0x08074B50, crc, len(data), len(content))
    extra = pack("<HHQQQ", 1, 24, len(content), len(data), 0) if zip64 else b""
    central = pack("<IHHHHHHIIIHHHHHII", 0x02014B50, version, version, flags, 8, 0, 0, crc,
                   0xFFFFFFFF if zip64 else len(data), 0xFFFFFFFF if zip64 else len(content),
                   len(name), len(extra), 0, 0, 0, 0, 0xFFFFFFFF if zip64 else 0) + name + extra
    suffix = b""
    if zip64:
        suffix += pack("<IQHHIIQQQQ", 0x06064B50, 44, version, version, 0, 0, 1, 1, len(central), len(local))
        suffix += pack("<IIQI", 0x07064B50, 0, len(local) + len(central), 1)
    suffix += pack("<IHHHHIIH", 0x06054B50, 0, 0, 0xFFFF if zip64 else 1, 0xFFFF if zip64 else 1,
                   0xFFFFFFFF if zip64 else len(central), 0xFFFFFFFF if zip64 else len(local), 0)
    return local + central + suffix

valid = {"standard.zip": standard(), "zip64.zip": handbuilt(zip64=True), "descriptor.zip": handbuilt(descriptor=True)}
valid["sfx.zip"] = b"SFX prefix: no executable data\n" + valid["standard.zip"]
empty = io.BytesIO()
with zipfile.ZipFile(empty, "w"):
    pass
valid["empty.zip"] = empty.getvalue()
for name, data in valid.items():
    (root / name).write_bytes(data)
bad = bytearray(valid["descriptor.zip"])
central = bad.index(b"PK\x01\x02")
bad[central + 16] ^= 1
(root / "bad-crc.zip").write_bytes(bad)
# Every prefix of a small ZIP64 exercises short scalar reads in EOCD, locator,
# local and central fields. Public API outcomes must match the reference.
for length in range(len(valid["zip64.zip"])):
    (root / ("truncated-%04d.zip" % length)).write_bytes(valid["zip64.zip"][:length])
