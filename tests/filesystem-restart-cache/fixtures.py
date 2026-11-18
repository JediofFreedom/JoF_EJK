import pathlib
import sys
import warnings
import zipfile

root = pathlib.Path(sys.argv[1])
warnings.filterwarnings('ignore', message='Duplicate name:')

def archive(path, entries):
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_STORED) as target:
        for name, data in entries:
            target.writestr(name, data)

archive(root / 'seed/base/pak0.pk3', [('mpdefault.cfg', b'base config'), ('same.bin', b'base'), ('only-base.bin', b'base only')])
archive(root / 'seed/base/pak9.pk3', [('same.bin', b'override'), ('dup.bin', b'first'), ('dup.bin', b'last duplicate'),
        ('empty/', b''), ('empty.bin', b''), ('cgamex86.dll', b'game'), ('uix86.dll', b'ui')] +
        [(f'entries/{n:04}.bin', str(n).encode()) for n in range(1200)])
archive(root / 'seed/EternalJK/client.pk3', [('client.bin', b'client')])
archive(root / 'seed/modA/mod.pk3', [('mod.bin', b'alpha')])
archive(root / 'seed/modB/mod.pk3', [('mod.bin', b'bravo')])
archive(root / 'seed/cmdmod/command.pk3', [('command.bin', b'command')])
archive(root / 'replacement.pk3', [('same.bin', b'replaced'), ('new.bin', b'new content')])
archive(root / 'added.pk3', [('new.bin', b'added')])
with zipfile.ZipFile(root / 'seed/base/pak9.pk3') as original:
    archive(root / 'same-size.pk3', [(entry.filename, b'replace!' if entry.filename == 'same.bin' else original.read(entry))
                                   for entry in original.infolist()])
(root / 'invalid.pk3').write_bytes(b'invalid archive root')
