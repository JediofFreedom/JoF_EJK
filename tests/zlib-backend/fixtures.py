"""Independent zlib streams, ZIP CRC failure and PNG decoded-byte fixtures."""
import pathlib
import struct
import sys
import zipfile
import zlib

root = pathlib.Path(sys.argv[1])
raw = bytes(range(256)) * 257 + b'player-model-texture' * 503
dictionary = b'player-model-texture'
(root / 'raw.bin').write_bytes(raw)
(root / 'dictionary.bin').write_bytes(dictionary)
for name, bits in [('raw.deflate', -15), ('wrapped.zlib', 15), ('wrapped.gzip', 31)]:
    codec = zlib.compressobj(wbits=bits)
    (root / name).write_bytes(codec.compress(raw) + codec.flush())
codec = zlib.compressobj(zdict=dictionary)
(root / 'dictionary.zlib').write_bytes(codec.compress(raw) + codec.flush())
with zipfile.ZipFile(root / 'stream.pk3', 'w') as archive:
    archive.writestr('compressed.bin', raw, compress_type=zipfile.ZIP_DEFLATED)
    archive.writestr('stored.bin', raw, compress_type=zipfile.ZIP_STORED)
    archive.writestr('empty', b'', compress_type=zipfile.ZIP_DEFLATED)
with zipfile.ZipFile(root / 'crc-bad.pk3', 'w', zipfile.ZIP_STORED) as archive:
    archive.writestr('stored.bin', raw)
bad = bytearray((root / 'crc-bad.pk3').read_bytes())
name_length, extra_length = struct.unpack_from('<HH', bad, 26)
bad[30 + name_length + extra_length + 3] ^= 1
(root / 'crc-bad.pk3').write_bytes(bad)

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

def png(name, depth, color, rows, rgba, extra=b'', interlace=0):
    header = struct.pack('>IIBBBBB', 2, 2, depth, color, 0, 0, interlace)
    encoded = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + extra +
               chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
    (root / (name + '.png')).write_bytes(encoded)
    (root / (name + '.rgba')).write_bytes(bytes(rgba))

rgba = bytes([10, 20, 30, 255, 40, 50, 60, 0, 70, 80, 90, 128, 100, 110, 120, 255])
png('rgba', 8, 6, b'\0' + rgba[:8] + b'\0' + rgba[8:], rgba)
values16 = b''.join(bytes([value, 23]) for value in rgba)
png('rgba16', 16, 6, b'\0' + values16[:16] + b'\0' + values16[16:], rgba)
png('palette', 8, 3, b'\0\0\1\0\1\0',
    [10, 20, 30, 255, 40, 50, 60, 0, 40, 50, 60, 0, 10, 20, 30, 255],
    chunk(b'PLTE', bytes([10, 20, 30, 40, 50, 60])) + chunk(b'tRNS', b'\xff\0'))
png('gray-low', 1, 0, b'\0\x40\0\x80',
    [0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255])
# For 2x2 RGBA, Adam7's nonempty passes are (0,0), (1,0), and row y=1.
png('adam7', 8, 6, b'\0' + rgba[:4] + b'\0' + rgba[4:8] + b'\0' + rgba[8:], rgba, interlace=1)
encoded = (root / 'rgba.png').read_bytes()
(root / 'truncated.png').write_bytes(encoded[:-15])
bad = bytearray(encoded)
idat = bad.index(b'IDAT')
size = struct.unpack_from('>I', bad, idat - 4)[0]
bad[idat + 4 + size] ^= 1
(root / 'crc-bad.png').write_bytes(bad)
