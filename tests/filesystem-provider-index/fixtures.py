from pathlib import Path
import sys
import warnings
import zipfile
root = Path(sys.argv[1])
(root/'middle').mkdir(exist_ok=True)
with warnings.catch_warnings():
    warnings.simplefilter('ignore', UserWarning)
    with zipfile.ZipFile(root/'duplicates.pk3', 'w', zipfile.ZIP_DEFLATED) as package:
        package.writestr('same.jpg', b'first')
        package.writestr('same.jpg', b'last duplicate')
        package.writestr('other.jpg', b'other')
