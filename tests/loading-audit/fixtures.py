"""Generated archives, with ZIP64 directory and executable-prefix coverage."""
import pathlib
import struct
import sys
import zipfile
import zlib

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

def png_chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

def png_fixture(name, depth, color, rows, rgba, extra=b''):
    header = struct.pack('>IIBBBBB', 2, 2, depth, color, 0, 0, 0)
    (root / (name + '.png')).write_bytes(b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', header) + extra +
        png_chunk(b'IDAT', zlib.compress(b''.join(b'\0' + row for row in rows))) + png_chunk(b'IEND', b''))
    (root / (name + '.rgba')).write_bytes(bytes(rgba))

rgba = [10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255]
for channels, color, name in [(3, 2, 'rgb'), (4, 6, 'rgba')]:
    values = [rgba[i * 4 + c] for i in range(4) for c in range(channels)]
    png_fixture(name, 8, color, [bytes(values[:channels*2]), bytes(values[channels*2:])], rgba)
    values16 = b''.join(bytes([v, 23]) for v in values)
    png_fixture(name+'16', 16, color, [values16[:channels*4], values16[channels*4:]], rgba)
png_fixture('palette', 8, 3, [b'\0\1', b'\1\0'], [10,20,30,255,40,50,60,0,40,50,60,0,10,20,30,255],
    png_chunk(b'PLTE', bytes([10,20,30,40,50,60])) + png_chunk(b'tRNS', b'\xff\0'))
png_fixture('gray', 8, 0, [b'\x0a\x14', b'\x1e\x28'], [v for g in [10,20,30,40] for v in [g,g,g,255]])
png_fixture('gray-alpha', 8, 4, [b'\x0a\xff\x14\0', b'\x1e\x80\x28\xff'],
    [10,10,10,255,20,20,20,0,30,30,30,128,40,40,40,255])
png_fixture('gray-low', 1, 0, [b'\x40', b'\x80'], [0,0,0,255,255,255,255,255,255,255,255,255,0,0,0,255])
