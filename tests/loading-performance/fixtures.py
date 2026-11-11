"""Small real ZIP fixtures, including duplicate names and malformed catalogues."""
import pathlib
import struct
import sys
import zipfile

directory = pathlib.Path(sys.argv[1])
with zipfile.ZipFile(directory / "fixture.pk3", "w", zipfile.ZIP_DEFLATED) as archive:
    archive.writestr("shaders/Override.shader", b"override")
    archive.writestr("shaders/override.shader", b"later")
    archive.writestr("empty/", b"")
    archive.writestr("empty/file.txt", b"")
    for name in ["textures/zeta/second.tga", "textures/Alpha/first.tga",
                 "textures/zeta/first.tga", "textures/ZETA/SECOND.tga",
                 "textures/zeta/deep/more/hidden.tga", "textures/zetail/extra.tga"]:
        archive.writestr(name, b"image")
    for index in range(6000):
        archive.writestr(f"models/players/test{index:05}/model.skin", f"entry {index}")

with zipfile.ZipFile(directory / "long-name.pk3", "w") as archive:
    archive.writestr("x" * 256, b"invalid qpath")

damaged = bytearray((directory / "fixture.pk3").read_bytes())
eocd = damaged.rindex(b"PK\x05\x06")
central = struct.unpack_from("<I", damaged, eocd + 16)[0]
damaged[central:central + 4] = b"BAD!"
(directory / "damaged.pk3").write_bytes(damaged)
