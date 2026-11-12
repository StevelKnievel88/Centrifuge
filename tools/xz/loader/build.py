"""Builds the Centrifuge loader payload for the XDJ-XZ (firmware 1.26) from src/ into payload/.

    python tools/xz/loader/build.py            (needs: pip install ziglang pyelftools pillow numpy)

src/ is the MIT / MPL-2.0 XZ Mods runtime by OpticMystic (https://github.com/OpticMystic/xdj-xz-toolkit) with
Centrifuge's changes: Centrifuge theme and names, DRUM/BASS - INST - VOCALS faders, settings under Centrifuge/,
and the mixer knob capture. payload/ holds what goes into autoexec.bin next to the patched player, which the
Windows tool makes on the user's PC from their own firmware download (no Pioneer code is kept in this repository).
"""
from __future__ import annotations

import hashlib
import os
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
SRC = HERE / 'src'
PAYLOAD = HERE / 'payload'
BRANDING = HERE.parents[2] / 'resources' / 'branding'


def rgb565(image: Image.Image) -> bytes:
    rgb = np.asarray(image.convert('RGB'), dtype=np.uint16)
    return (((rgb[:, :, 0] >> 3) << 11) | ((rgb[:, :, 1] >> 2) << 5) | (rgb[:, :, 2] >> 3)).astype('<u2').tobytes()


def logo_strip() -> Image.Image:
    """560 x 92: the Centrifuge wordmark where the stock screen shows its own logo with no track loaded."""
    art = Image.open(BRANDING / 'centrifuge-splash-800x480.png').convert('RGB')
    lum = np.asarray(art.convert('L'))
    ys, xs = np.where(lum > 40)
    art = art.crop((int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1))
    art.thumbnail((540, 84), Image.Resampling.LANCZOS)
    out = Image.new('RGB', (560, 92), (5, 4, 9))
    out.paste(art, ((560 - art.width) // 2, (92 - art.height) // 2))
    return out


def main() -> int:
    zig = HERE / '.zig-wrapper'
    zig.write_text('#!/bin/sh\nexec "%s" -m ziglang "$@"\n' % sys.executable)
    zig.chmod(zig.stat().st_mode | stat.S_IEXEC)
    with tempfile.TemporaryDirectory(prefix='cf-xz-build-') as tmp:
        out = Path(tmp)
        subprocess.run([sys.executable, str(SRC / 'mods' / 'build.py'), '--zig', str(zig), '--output', str(out)], check=True)
        if PAYLOAD.exists():
            shutil.rmtree(PAYLOAD)
        (PAYLOAD / 'tools').mkdir(parents=True)
        shutil.copyfile(out / 'libxz-mods-development.so', PAYLOAD / 'tools' / 'libxz-mods.so')
        shutil.copyfile(out / 'libxz-directfb-mods-test.so', PAYLOAD / 'tools' / 'libxz-directfb-hook.so')
    zig.unlink()

    script = (SRC / 'vendor/tools/xz_runtime/orchestrator.sh').read_bytes().replace(b'\r\n', b'\n')
    (PAYLOAD / 'autoexec.sh').write_bytes(script)
    (PAYLOAD / 'mods-mode').write_bytes(b'experimental\n')
    (PAYLOAD / 'knob-capture').write_bytes(b'1\n')

    brand = PAYLOAD / 'branding'
    brand.mkdir()
    splash = Image.open(BRANDING / 'centrifuge-splash-800x480.png').convert('RGB')
    assert splash.size == (800, 480)
    (brand / 'splash.rgb565').write_bytes(rgb565(splash))
    (brand / 'logo.rgb565').write_bytes(rgb565(logo_strip()))
    # Same in-RAM patch points as the XZ Mods branding step: the stock GUI pack's splash and unloaded-deck logo.
    (brand / 'apply.sh').write_bytes(b'''#!/bin/sh
set -eu
pack="$1"
assets=$(dirname "$0")
[ "$(wc -c < "$pack")" -eq 17666908 ]
[ "$(dd if="$pack" bs=44 count=1582 2>/dev/null | md5sum | cut -d ' ' -f 1)" = 1160eaec6cbb0e6e13ea22232a4b2edf ]
[ "$(wc -c < "$assets/splash.rgb565")" -eq 768000 ]
[ "$(wc -c < "$assets/logo.rgb565")" -eq 103040 ]
dd if="$assets/splash.rgb565" of="$pack" bs=8 seek=1541365 conv=notrunc 2>/dev/null
dd if="$assets/logo.rgb565" of="$pack" bs=8 seek=1779173 conv=notrunc 2>/dev/null
''')
    names = ['splash.rgb565', 'logo.rgb565', 'apply.sh']
    (brand / 'MD5SUMS').write_bytes(''.join(hashlib.md5((brand / n).read_bytes()).hexdigest() + '  ' + n + '\n' for n in names).encode('ascii'))

    lic = PAYLOAD / 'licenses'
    lic.mkdir()
    for original, name in (('LICENSE-XZ-MODS', 'XZ-Mods-MIT.txt'), ('THIRD-PARTY-NOTICES.md', 'THIRD-PARTY-NOTICES.md'),
                           ('mods/key/LICENSE-MPL-2.0', 'Mozilla-MPL-2.0.txt'), ('mods/ui/fonts/OFL.txt', 'Barlow-OFL.txt'),
                           ('mods/audio/licenses/LICENSE-MIT', 'cdj3k-mods-MIT.txt'), ('mods/audio/licenses/LICENSE-APACHE', 'cdj3k-mods-APACHE.txt'),
                           ('mods/audio/vendor/dr_libs/LICENSE', 'dr-libs.txt'), ('mods/audio/vendor/miniz/LICENSE', 'miniz-MIT.txt'),
                           ('mods/audio/vendor/jsmn/LICENSE', 'jsmn-MIT.txt')):
        if (SRC / original).is_file():
            shutil.copyfile(SRC / original, lic / name)
    (lic / 'README.txt').write_text(
        'The Centrifuge loader for the XDJ-XZ is built on the XZ Mods runtime by OpticMystic / VJ.Tools\n'
        '(https://github.com/OpticMystic/xdj-xz-toolkit), used under the MIT and MPL-2.0 licences in this folder.\n'
        'Source for this build, including Centrifuge\'s changes: tools/xz/loader/src in the Centrifuge repository.\n')
    for path in sorted(PAYLOAD.rglob('*')):
        if path.is_file():
            print(f'{path.stat().st_size:>9}  {path.relative_to(PAYLOAD).as_posix()}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
