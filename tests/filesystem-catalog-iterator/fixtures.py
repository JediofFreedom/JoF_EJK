import io
import pathlib
import struct
import sys
import warnings
import zipfile

root = pathlib.Path(sys.argv[1])
output = io.BytesIO()
with warnings.catch_warnings(), zipfile.ZipFile(output, 'w') as archive:
    warnings.simplefilter('ignore', UserWarning)
    for name, data in [('empty', b''), ('same.bin', b'first'), ('same.bin', b'last'),
                       ('uix86.dll', b'ui'), ('cgamex86.dll', b'cgame'), ('Textures/Mixed.TGA', b'mixed'),
                       ('unicode/é.txt', b'unicode')]:
        item = zipfile.ZipInfo(name)
        item.comment = b'entry-comment'
        item.extra = struct.pack('<HH4s', 0xcafe, 4, b'test')
        item.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(item, data)
valid = output.getvalue()
(root / 'catalog.pk3').write_bytes(valid)
(root / 'catalog-sfx.pk3').write_bytes(b'non-executable prefix\n' + valid)
damaged = bytearray(valid)
first = damaged.index(b'PK\x01\x02')
damaged[first] ^= 1
(root / 'catalog-damaged.pk3').write_bytes(damaged)
output = io.BytesIO()
with zipfile.ZipFile(output, 'w') as archive:
    archive.writestr('x' * 256, b'long-name')
(root / 'catalog-long.pk3').write_bytes(output.getvalue())
# A well-formed central header with a filename extending beyond physical EOF.
truncated_name = bytearray(valid)
last = truncated_name.rfind(b'PK\x01\x02')
struct.pack_into('<H', truncated_name, last + 28, 65535)
(root / 'catalog-truncated-name.pk3').write_bytes(truncated_name)
# Preserve the legacy public uLong conversion for ZIP64 metadata beyond 4 GiB.
large = bytearray((root / 'zip64.zip').read_bytes())
central = large.index(b'PK\x01\x02')
name_size = struct.unpack_from('<H', large, central + 28)[0]
struct.pack_into('<Q', large, central + 46 + name_size + 4, 0x100000000)
(root / 'zip64-large-metadata.zip').write_bytes(large)
