"""Generated archives, with ZIP64 directory and executable-prefix coverage."""
import pathlib
import struct
import sys
import zipfile

root = pathlib.Path(sys.argv[1])
for name, count in [('ordinary.pk3', 5), ('zip64.pk3', 65536)]:
    with zipfile.ZipFile(root / name, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('first', bytes(range(256)) * 1024)
        archive.writestr('second', b'independent reader' * 4096)
        for i in range(count):
            archive.writestr(f'empty/{i}', b'')
    prefix = b'Executable prefix\0' * 7
    payload = bytearray((root / name).read_bytes())
    if name == 'zip64.pk3':
        # The ZIP64 locator is an absolute disk offset, including the stub.
        locator = payload.rindex(b'PK\x06\x07')
        offset = struct.unpack_from('<Q', payload, locator + 8)[0]
        struct.pack_into('<Q', payload, locator + 8, offset + len(prefix))
    (root / ('sfx-' + name)).write_bytes(prefix + payload)
with zipfile.ZipFile(root / 'empty.pk3', 'w'):
    pass
# Classic minizip uses offset zero as its missing-directory sentinel.
(root / 'empty.pk3').write_bytes(b'SFX' + (root / 'empty.pk3').read_bytes())
