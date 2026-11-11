"""Centrifuge XZ tools (Windows) - groundwork for the Centrifuge loader on the XDJ-XZ (firmware 1.26).

  centrifuge-xz.exe extract            downloads the official 1.26 firmware (AlphaTheta) and Pioneer's published
                                       source archive (for the boot key), checks both, and writes
                                       "Centrifuge XZ\\xz-1.26-player.zip" on the desktop (the player program files)

  centrifuge-xz.exe testloader E:\\     writes a boot TEST loader to the USB drive E:\\ (autoexec.bin). It's a
                                       proof-of-life only: at power-on the XDJ-XZ runs it, it writes
                                       CENTRIFUGE-BOOT-OK.txt back to the stick, and the deck then boots 100%
                                       normally. It does NOT change the player, touch the firmware, or keep any
                                       state - power off and remove the stick and the XZ is completely standard.

Run with no arguments (or double-click) for a menu. Downloads are cached in %LOCALAPPDATA%\\Centrifuge\\xz.

The download / boot-support / image-authoring steps follow the MIT-licensed XZ Mods builder by OpticMystic
(https://github.com/OpticMystic/xdj-xz-toolkit), with thanks.
"""
from __future__ import annotations

import hashlib
import io
import os
import struct
import sys
import tarfile
import urllib.request
import zipfile
import zlib
from pathlib import Path

import pycdlib
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from inflate64 import Inflater

FIRMWARE = {'filename': 'XDJXZ_v126.zip',
            'url': 'https://downloads.support.alphatheta.com/firmwares/all-in-one-dj-systems/XDJ-XZ/XDJXZ_v126.zip',
            'sha256': '7c4159ca90b0cfd651725a68e10a11cf1a621239f703a0071f24355cc89fed7f', 'bytes': 70775680}
SOURCE_PARTS = [
    {'filename': 'pioneerdj_xdj_xz.tar.bz2.00.zip', 'member': 'pioneerdj_xdj_xz.tar.bz2.00',
     'url': 'https://files.microcms-assets.io/assets/3b9e29ce734e49babfedb3f8d1e728e3/c111011dece644809c0b8fb98cd48bc3/CDBEF6FF-4AEB-4CC2-93FE-16B8D8EC9132.zip',
     'bytes': 209448413, 'sha256': '81b52d25a1df0890cd83641eae3d705a1cfcea801fc7f9918b7a40bd39e9ee36'},
    {'filename': 'pioneerdj_xdj_xz.tar.bz2.01.zip', 'member': 'pioneerdj_xdj_xz.tar.bz2.01',
     'url': 'https://files.microcms-assets.io/assets/3b9e29ce734e49babfedb3f8d1e728e3/3936f89830bb4eebaf486b2d4a632112/B42B6EDE-945B-418A-A9DF-E954C8D501ED.zip',
     'bytes': 52468250, 'sha256': '9c7094b0c958a5928141bd091b150e01b66bbde49a40a275588c6f9a47578eaf'},
]
KEY_SHA256 = '912e9c3f3090378090e88ac026abda797cbf941492d2b469a05b1e3a57fcc6c5'
INITRAMFS = 'pioneerdj_xdj_xz/initramfs.tar.gz'
KEY_MEMBER = 'initramfs/usr/local/pdj/aes256.key'
STOCK_RBP_SHA256 = '6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2'


def say(text: str) -> None:
    print(text, flush=True)


def data_root() -> Path:
    base = Path(os.environ.get('LOCALAPPDATA', Path.home() / '.local' / 'share')) / 'Centrifuge' / 'xz'
    base.mkdir(parents=True, exist_ok=True)
    return base


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def download(record: dict, folder: Path) -> Path:
    folder.mkdir(parents=True, exist_ok=True)
    target = folder / record['filename']
    if target.is_file() and target.stat().st_size == record['bytes'] and sha256_file(target) == record['sha256']:
        say(f"  {record['filename']}: already downloaded")
        return target
    part = target.with_suffix(target.suffix + '.part')
    request = urllib.request.Request(record['url'], headers={'User-Agent': 'Centrifuge-XZ/1.0'})
    h = hashlib.sha256()
    got = 0
    with urllib.request.urlopen(request, timeout=30) as response, part.open('wb') as out:
        if not response.geturl().startswith('https://'):
            raise ValueError('Download redirected away from HTTPS')
        last = -1
        while True:
            block = response.read(1 << 20)
            if not block:
                break
            out.write(block)
            h.update(block)
            got += len(block)
            if got > record['bytes']:
                raise ValueError('Download is larger than expected')
            pct = got * 100 // record['bytes']
            if pct // 5 != last:
                last = pct // 5
                print(f"\r  {record['filename']}: {pct}%   ", end='', flush=True)
    print()
    if got != record['bytes'] or h.hexdigest() != record['sha256']:
        part.unlink(missing_ok=True)
        raise ValueError(f"{record['filename']} failed its check (size or SHA-256) - try again")
    os.replace(part, target)
    return target


class _Deflate64Member:
    """One Deflate64 ZIP member, read as a stream (Pioneer's source archive uses Deflate64)."""

    def __init__(self, archive: zipfile.ZipFile, entry: zipfile.ZipInfo):
        self.file = open(archive.filename, 'rb')
        self.file.seek(entry.header_offset)
        header = self.file.read(30)
        if len(header) != 30 or header[:4] != b'PK\x03\x04':
            raise ValueError('Invalid source ZIP header')
        name_len, extra_len = struct.unpack_from('<HH', header, 26)
        self.file.seek(entry.header_offset + 30 + name_len + extra_len)
        self.remaining = entry.compress_size
        self.expected = entry.file_size
        self.crc_expected = entry.CRC
        self.inflater = Inflater()
        self.buffer = bytearray()
        self.total = 0
        self.crc = 0

    def read(self, size: int) -> bytes:
        while len(self.buffer) < size and self.remaining:
            block = self.file.read(min(self.remaining, 1 << 20))
            if not block:
                raise ValueError('Source ZIP member is truncated')
            self.remaining -= len(block)
            decoded = self.inflater.inflate(block)
            self.total += len(decoded)
            self.crc = zlib.crc32(decoded, self.crc)
            self.buffer.extend(decoded)
        if not self.remaining and (self.total != self.expected or self.crc != self.crc_expected):
            raise ValueError('Source ZIP member failed its CRC check')
        result = bytes(self.buffer[:size])
        del self.buffer[:size]
        return result

    def close(self) -> None:
        self.file.close()


class _Parts:
    def __init__(self, streams):
        self.streams = iter(streams)
        self.current = next(self.streams, None)

    def read(self, size: int) -> bytes:
        chunks, left = [], size
        while left and self.current is not None:
            data = self.current.read(left)
            if data:
                chunks.append(data)
                left -= len(data)
            else:
                self.current = next(self.streams, None)
        return b''.join(chunks)


def boot_key() -> bytes:
    root = data_root() / 'boot-support'
    root.mkdir(exist_ok=True)
    cached = root / 'aes256.key'
    if cached.is_file() and hashlib.sha256(cached.read_bytes()).hexdigest() == KEY_SHA256:
        raw = cached.read_bytes()
    else:
        say("Getting Pioneer's published XDJ-XZ source archive (about 250 MB, first time only)")
        paths = [download(r, root / 'downloads') for r in SOURCE_PARTS]
        say('Finding the boot support file in it (a few minutes)')
        raw = None
        members = []
        try:
            for path, record in zip(paths, SOURCE_PARTS):
                archive = zipfile.ZipFile(path)
                if archive.namelist() != [record['member']]:
                    raise ValueError('Unexpected source archive layout')
                members.append(_Deflate64Member(archive, archive.getinfo(record['member'])))
            with tarfile.open(fileobj=_Parts(members), mode='r|bz2') as outer:
                for member in outer:
                    if member.name.lstrip('./') != INITRAMFS:
                        continue
                    nested = outer.extractfile(member).read()
                    with tarfile.open(fileobj=io.BytesIO(nested), mode='r:gz') as inner:
                        for entry in inner:
                            if entry.name.lstrip('./') == KEY_MEMBER:
                                raw = inner.extractfile(entry).read()
                                break
                    break
        finally:
            for m in members:
                m.close()
        if raw is None or hashlib.sha256(raw).hexdigest() != KEY_SHA256:
            raise ValueError('The boot support file was not found or failed its check')
        cached.write_bytes(raw)
    line = raw.splitlines()[0]
    return line[:31].ljust(32, b'\0')


def decrypt(data: bytes, key: bytes) -> bytes:
    out = bytearray(len(data))
    for start in range(0, len(data), 512):
        c = Cipher(algorithms.AES(key), modes.CBC(struct.pack('<I', start // 512) + bytes(12))).decryptor()
        out[start:start + 512] = c.update(data[start:start + 512]) + c.finalize()
    return bytes(out)


def extract() -> int:
    say('Centrifuge XZ tools - extract the XDJ-XZ 1.26 player program\n')
    say('Getting the official XDJ-XZ 1.26 firmware from AlphaTheta (about 70 MB)')
    fw = download(FIRMWARE, data_root() / 'firmware')
    key = boot_key()
    say('Opening the firmware')
    with zipfile.ZipFile(fw) as z:
        upd = [i for i in z.infolist() if i.filename.lower().endswith('.upd')]
        if len(upd) != 1:
            raise ValueError('Unexpected firmware ZIP')
        data = z.read(upd[0])
    data = decrypt(data[:len(data) // 512 * 512], key)
    if data[32769:32774] != b'CD001':
        raise ValueError('The firmware did not decrypt - check the downloads')
    iso = pycdlib.PyCdlib()
    iso.open_fp(io.BytesIO(data))

    out_dir = Path(os.environ.get('USERPROFILE', Path.home())) / 'Desktop' / 'Centrifuge XZ'
    out_dir.mkdir(parents=True, exist_ok=True)
    out_zip = out_dir / 'xz-1.26-player.zip'
    listing = []
    rbp_sha = None
    with zipfile.ZipFile(out_zip, 'w', zipfile.ZIP_DEFLATED) as z:
        for directory, _, files in iso.walk(rr_path='/'):
            for name in files:
                path = directory.rstrip('/') + '/' + name
                buf = io.BytesIO()
                try:
                    iso.get_file_from_iso_fp(buf, rr_path=path)
                except Exception as e:  # noqa: BLE001 - symlinks (and anything else without data) are listed, not copied
                    target = ''
                    try:
                        rec = iso.get_record(rr_path=path)
                        if rec.rock_ridge is not None and rec.rock_ridge.is_symlink():
                            target = rec.rock_ridge.symlink_path().decode('utf-8', 'replace')
                    except Exception:  # noqa: BLE001
                        pass
                    listing.append(f'{"link":>12}  {path} -> {target or e}')
                    continue
                content = buf.getvalue()
                listing.append(f'{len(content):>12}  {path}')
                # everything in the update image (the player program, its libraries and settings)
                if len(content) <= 64 * 1024 * 1024:
                    z.writestr('iso' + path, content)
                    if name == 'rbp':
                        rbp_sha = hashlib.sha256(content).hexdigest()
                if name in ('pdj', 'pdj.tar.gz', 'pdj.tgz') or name.endswith(('.tar.gz', '.tgz', '.tar')):
                    try:
                        with tarfile.open(fileobj=io.BytesIO(content), mode='r:*') as t:
                            for m in t:
                                if not m.isfile():
                                    continue
                                listing.append(f'{m.size:>12}  {path}!/{m.name}')
                                if m.size <= 64 * 1024 * 1024:
                                    body = t.extractfile(m).read()
                                    z.writestr('pdj/' + m.name.lstrip('./'), body)
                                    if m.name.rsplit('/', 1)[-1] == 'rbp':
                                        rbp_sha = hashlib.sha256(body).hexdigest()
                    except tarfile.TarError:
                        pass
        z.writestr('listing.txt', '\n'.join(listing) + '\n')
    iso.close()
    say('')
    if rbp_sha is None:
        say('Warning: the player program (rbp) was not found - send the zip anyway, its listing helps')
    elif rbp_sha != STOCK_RBP_SHA256:
        say('Warning: the player program is not the expected 1.26 version')
    else:
        say('Player program found and checked (XDJ-XZ 1.26)')
    say(f'\nDone: {out_zip}  ({out_zip.stat().st_size // (1024 * 1024)} MB)')
    say('Attach that zip in the Claude chat.')
    return 0


def encrypt(data: bytes, key: bytes) -> bytes:
    out = bytearray(len(data))
    for start in range(0, len(data), 512):
        c = Cipher(algorithms.AES(key), modes.CBC(struct.pack('<I', start // 512) + bytes(12))).encryptor()
        out[start:start + 512] = c.update(data[start:start + 512]) + c.finalize()
    return bytes(out)


def author_autoexec(files: dict, key: bytes) -> bytes:
    """Builds an autoexec.bin: a Rock Ridge ISO9660 of `files` (name -> bytes, at the root), cryptoloop-AES-encrypted
    exactly as the XDJ-XZ's decrypt_autoexec.sh expects (512-byte sectors, little-endian sector-index IV)."""
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3, vol_ident='UsbAuto', rock_ridge='1.09')
    n = 0
    for name, data in files.items():
        n += 1
        iso.add_fp(io.BytesIO(data), len(data), f'/F{n:07d};1', rr_name=name, file_mode=0o100755)
    out = io.BytesIO()
    iso.write_fp(out)
    iso.close()
    data = bytearray(out.getvalue())
    data.extend(bytes(150 * 2048))                       # the deck's loop image expects some slack
    blocks = len(data) // 2048
    struct.pack_into('<I', data, 32768 + 80, blocks)     # volume space size (both-endian), matching the padded length
    struct.pack_into('>I', data, 32768 + 84, blocks)
    return encrypt(bytes(data), key)


# A proof-of-life boot test ONLY. It writes a file back to the stick and returns, so the XDJ-XZ then boots exactly as
# normal. It does not stage the player, load anything, or keep any state. The stick may be mounted read-only during
# early boot, so it remounts it read-write and also writes to any other FAT mount; a deliberate ~8 s pause at the end
# is a visible tell that the script ran even if no file can be written.
BOOT_TEST_SH = (
    "#!/bin/sh\n"
    "# Centrifuge XZ boot test v2 (proof of life). The deck boots normally after this; nothing is modified or kept.\n"
    "USB=\"$1\"\n"
    "mark() {\n"
    "  {\n"
    "    echo 'Centrifuge XZ boot test - it ran'\n"
    "    echo \"when   : $(date 2>/dev/null)\"\n"
    "    echo \"target : $1\"\n"
    "    echo \"uname  : $(uname -a 2>/dev/null)\"\n"
    "    echo \"serial : $(fw_printenv -n serial_num 2>/dev/null)\"\n"
    "    echo \"rbp    : $(ls -l /root/pdj/rbp 2>/dev/null)\"\n"
    "    echo 'mounts :'\n"
    "    cat /proc/mounts 2>/dev/null\n"
    "  } > \"$1/CENTRIFUGE-BOOT-OK.txt\" 2>/dev/null\n"
    "}\n"
    "# the stick may be read-only this early in boot: remount it read-write, then write\n"
    "mount -o remount,rw \"$USB\" 2>/dev/null\n"
    "mark \"$USB\"\n"
    "# also try every FAT / exFAT mount, in case the stick is mounted somewhere else\n"
    "cat /proc/mounts 2>/dev/null | while read dev mp fs rest; do\n"
    "  case \"$fs\" in\n"
    "    vfat|fat|exfat|msdos)\n"
    "      mount -o remount,rw \"$mp\" 2>/dev/null\n"
    "      mark \"$mp\" ;;\n"
    "  esac\n"
    "done\n"
    "sync\n"
    "# visible tell: if no file survives, a ~8 s longer boot still proves the script ran\n"
    "sleep 8\n"
    "exit 0\n"
).encode('ascii')


def resource(name: str) -> Path:
    """A bundled file, whether frozen (PyInstaller) or run from the source tree."""
    base = getattr(sys, '_MEIPASS', None)
    return (Path(base) / 'bin' / name) if base else (Path(__file__).resolve().parent / 'bin' / name)


# Boot-splash payload: shows the Centrifuge logo on the XZ screen (/dev/fb0, 800x480 RGB565) during boot, then lets
# the deck carry on and take the screen over as normal. It only writes pixels to the framebuffer; nothing is kept.
SPLASH_SH = (
    "#!/bin/sh\n"
    "# Centrifuge XZ boot splash. Draws the logo to the screen during boot; the deck then boots normally.\n"
    "cp ./centrifuge-splash.rgb565 /tmp/cf-splash.565 2>/dev/null\n"
    "(\n"
    "  cd /\n"
    "  n=0\n"
    "  while [ $n -lt 60 ]; do\n"
    "    cat /tmp/cf-splash.565 > /dev/fb0 2>/dev/null\n"
    "    n=$((n+1))\n"
    "    usleep 300000 2>/dev/null || sleep 1\n"
    "  done\n"
    ") &\n"
    "exit 0\n"
).encode('ascii')


# Screen prototype: launches cfxz-screen, which draws the Centrifuge UI to /dev/fb0 (spinning platter + playhead)
# over the normal screen. Screen-only; nothing else on the deck is touched, nothing persists.
SCREEN_SH = (
    "#!/bin/sh\n"
    "# Centrifuge screen prototype. Draws the Centrifuge UI to the framebuffer; the deck otherwise runs as normal.\n"
    "USB=\"$1\"\n"
    "cp ./cfxz-screen /tmp/cfxz-screen 2>/dev/null\n"
    "cp ./centrifuge-screen.rgb565 /tmp/cf-screen.565 2>/dev/null\n"
    "chmod +x /tmp/cfxz-screen 2>/dev/null\n"
    "(\n"
    "  cd /\n"
    "  sleep 12\n"                      # let the deck finish booting and own the screen first
    "  mount -o remount,rw \"$USB\" 2>/dev/null\n"
    "  {\n"
    "    echo '--- /proc/fb ---'; cat /proc/fb 2>/dev/null\n"
    "    for d in /sys/class/graphics/fb0 /sys/class/graphics/fb1; do\n"
    "      echo \"--- $d ---\"\n"
    "      echo \"virtual_size: $(cat $d/virtual_size 2>/dev/null)\"\n"
    "      echo \"bits_per_pixel: $(cat $d/bits_per_pixel 2>/dev/null)\"\n"
    "      echo \"stride: $(cat $d/stride 2>/dev/null)\"\n"
    "      echo \"modes: $(cat $d/modes 2>/dev/null)\"\n"
    "      echo \"blank: $(cat $d/blank 2>/dev/null)\"\n"
    "    done\n"
    "  } > \"$USB/cf-screen-sys.txt\" 2>&1\n"
    "  sync\n"
    "  /tmp/cfxz-screen /tmp/cf-screen.565 \"$USB/cf-screen.log\" >/dev/null 2>&1\n"
    ") &\n"
    "exit 0\n"
).encode('ascii')


def screen(volume: str, force: bool = False) -> int:
    say('Centrifuge XZ tools - write the SCREEN prototype to a USB stick\n')
    vol = Path(volume)
    if not vol.is_dir():
        say(f'That drive / folder is not there: {volume}')
        return 2
    dest = vol / 'autoexec.bin'
    if dest.exists() and not force:
        say(f'{dest} already exists. Run from the menu to replace it.')
        return 2
    prog = resource('cfxz-screen')
    bgimg = resource('centrifuge-screen.rgb565')
    if not prog.is_file() or not bgimg.is_file():
        say('The screen prototype files are missing from this build.')
        return 2
    key = boot_key()
    say('Building the screen prototype loader')
    image = author_autoexec({'autoexec.sh': SCREEN_SH, 'cfxz-screen': prog.read_bytes(),
                             'centrifuge-screen.rgb565': bgimg.read_bytes()}, key)
    tmp = vol / '.centrifuge-autoexec.tmp'
    tmp.write_bytes(image)
    os.replace(tmp, dest)
    say(f'\nWritten: {dest}  ({len(image) // 1024} KB)')
    say('\nPut the stick in the XZ while it is OFF, switch on, and let it finish booting.')
    say('About 10 seconds after the normal screen appears, the Centrifuge screen takes over -')
    say('with a spinning platter and a moving playhead. The jog wheels, pads and audio still work as')
    say('normal underneath; only the screen is Centrifuge. Power off and remove the stick for the stock screen.')
    return 0


def splash(volume: str, force: bool = False) -> int:
    say('Centrifuge XZ tools - write a boot SPLASH loader to a USB stick\n')
    vol = Path(volume)
    if not vol.is_dir():
        say(f'That drive / folder is not there: {volume}')
        return 2
    dest = vol / 'autoexec.bin'
    if dest.exists() and not force:
        say(f'{dest} already exists. Run from the menu to replace it.')
        return 2
    img = resource('centrifuge-splash.rgb565')
    if not img.is_file():
        say('The splash image is missing from this build.')
        return 2
    key = boot_key()
    say('Building the splash loader')
    image = author_autoexec({'autoexec.sh': SPLASH_SH, 'centrifuge-splash.rgb565': img.read_bytes()}, key)
    tmp = vol / '.centrifuge-autoexec.tmp'
    tmp.write_bytes(image)
    os.replace(tmp, dest)
    say(f'\nWritten: {dest}  ({len(image) // 1024} KB)')
    say('\nPut the stick in the XZ while it is OFF, switch on, and watch the screen during start-up -')
    say('the Centrifuge logo should appear for a few seconds, then the normal screen takes over.')
    say('It only draws to the screen; it changes nothing on the XZ and keeps nothing.')
    return 0


# Reconnaissance boot payload. It runs in the background (so the deck boots normally), captures read-only system /
# device / memory-map info to the stick, and runs cfxz-peek to sample the mixer object while you turn the knobs.
# It changes nothing on the player, the device or the firmware.
RECON_SH = (
    "#!/bin/sh\n"
    "# Centrifuge XZ reconnaissance (read-only). The deck boots normally; nothing is modified or kept.\n"
    "USB=\"$1\"\n"
    "# autoexec.sh runs from inside the (read-only, soon-unmounted) image, so copy the helper out now\n"
    "cp ./cfxz-peek /tmp/cfxz-peek 2>/dev/null\n"
    "chmod +x /tmp/cfxz-peek 2>/dev/null\n"
    "(\n"
    "  cd /                               # don't hold the image mount open\n"
    "  sleep 20                           # let the player come up\n"
    "  mount -o remount,rw \"$USB\" 2>/dev/null\n"
    "  D=\"$USB/Centrifuge-XZ-recon\"\n"
    "  mkdir -p \"$D\" 2>/dev/null\n"
    "  PID=$(pidof rbp 2>/dev/null)\n"
    "  {\n"
    "    echo '=== uname ==='; uname -a\n"
    "    echo '=== cmdline ==='; cat /proc/cmdline\n"
    "    echo '=== date ==='; date\n"
    "    echo '=== rbp pid ==='; echo \"$PID\"\n"
    "    echo '=== fw_printenv ==='; fw_printenv 2>/dev/null\n"
    "    echo '=== lsmod ==='; lsmod\n"
    "    echo '=== mounts ==='; cat /proc/mounts\n"
    "    echo '=== cpuinfo ==='; cat /proc/cpuinfo\n"
    "  } > \"$D/system.txt\" 2>&1\n"
    "  {\n"
    "    echo '=== /dev ==='; ls -la /dev\n"
    "    echo '=== /dev/input ==='; ls -la /dev/input 2>/dev/null; cat /proc/bus/input/devices 2>/dev/null\n"
    "    echo '=== /dev/snd ==='; ls -la /dev/snd 2>/dev/null\n"
    "    echo '=== /dev/fb* ==='; ls -la /dev/fb* 2>/dev/null\n"
    "    echo '=== /sys/class ==='; ls /sys/class 2>/dev/null\n"
    "    echo '=== tty ==='; ls -la /dev/tty* 2>/dev/null\n"
    "  } > \"$D/devices.txt\" 2>&1\n"
    "  dmesg > \"$D/dmesg.txt\" 2>&1\n"
    "  { cat /proc/asound/cards 2>/dev/null; echo; ls -la /proc/asound 2>/dev/null; } > \"$D/asound.txt\" 2>&1\n"
    "  # framebuffer format + a screenshot of the XZ screen (for the Centrifuge boot screen)\n"
    "  {\n"
    "    echo 'fb0 virtual_size:'; cat /sys/class/graphics/fb0/virtual_size 2>/dev/null\n"
    "    echo 'fb0 bits_per_pixel:'; cat /sys/class/graphics/fb0/bits_per_pixel 2>/dev/null\n"
    "    echo 'fb0 stride:'; cat /sys/class/graphics/fb0/stride 2>/dev/null\n"
    "    echo 'fb0 modes:'; cat /sys/class/graphics/fb0/modes 2>/dev/null\n"
    "  } > \"$D/fb0.txt\" 2>&1\n"
    "  dd if=/dev/fb0 of=\"$D/fb0.raw\" bs=1024 count=1024 2>/dev/null\n"
    "  if [ -n \"$PID\" ]; then\n"
    "    cat /proc/$PID/maps   > \"$D/rbp-maps.txt\"   2>&1\n"
    "    ls -la /proc/$PID/fd  > \"$D/rbp-fd.txt\"     2>&1\n"
    "    cat /proc/$PID/status > \"$D/rbp-status.txt\" 2>&1\n"
    "    # probe whether the shell can read the player's memory at a known code address (0x25c98c)\n"
    "    { echo 'dd /proc/PID/mem @0x25c98c:'; dd if=/proc/$PID/mem bs=4 skip=619107 count=4 2>/dev/null | od -An -tx1; echo \"exit=$?\"; } > \"$D/mem-probe.txt\" 2>&1\n"
    "  fi\n"
    "  # sample the mixer object + the buffers it points to, for ~2 min; turn the CH3 & CH4 knobs throughout\n"
    "  if [ -x /tmp/cfxz-peek ] && [ -n \"$PID\" ]; then\n"
    "    /tmp/cfxz-peek \"$D/knobs.txt\" 120 700 2>\"$D/knobs-err.txt\"\n"
    "  fi\n"
    "  sync\n"
    ") &\n"
    "exit 0\n"
).encode('ascii')


def recon(volume: str, force: bool = False) -> int:
    say('Centrifuge XZ tools - write a RECON loader to a USB stick\n')
    vol = Path(volume)
    if not vol.is_dir():
        say(f'That drive / folder is not there: {volume}')
        return 2
    dest = vol / 'autoexec.bin'
    if dest.exists() and not force:
        say(f'{dest} already exists. Run from the menu to replace it.')
        return 2
    peek = resource('cfxz-peek')
    if not peek.is_file():
        say('The cfxz-peek helper is missing from this build.')
        return 2
    key = boot_key()
    say('Building the recon loader')
    image = author_autoexec({'autoexec.sh': RECON_SH, 'cfxz-peek': peek.read_bytes()}, key)
    tmp = vol / '.centrifuge-autoexec.tmp'
    tmp.write_bytes(image)
    os.replace(tmp, dest)
    say(f'\nWritten: {dest}  ({len(image) // 1024} KB)')
    say('\nTo run the recon on the XDJ-XZ (firmware 1.26):')
    say('  1. Put this USB stick in the XZ while it is switched OFF.')
    say('  2. Switch on and wait for it to finish booting (it boots as normal).')
    say('  3. From the moment the normal screen appears, keep turning the CHANNEL 3 and CHANNEL 4')
    say('     knobs - TRIM, HI, MID, LOW - fully one way then fully the other, over and over, slowly,')
    say('     for about TWO MINUTES. Keep going the whole time so the sweep is sure to be captured.')
    say('  4. Switch off, put the stick back in this PC, and send Claude the whole')
    say('     "Centrifuge-XZ-recon" folder from the stick (zip it).')
    say('\nIt only reads information; it changes nothing on the XZ and keeps nothing.')
    return 0


def testloader(volume: str, force: bool = False) -> int:
    say('Centrifuge XZ tools - write a boot TEST loader to a USB stick\n')
    vol = Path(volume)
    if not vol.is_dir():
        say(f'That drive / folder is not there: {volume}')
        say('Pass the USB drive, e.g.  "Centrifuge XZ Tools.exe" testloader E:\\')
        return 2
    dest = vol / 'autoexec.bin'
    if dest.exists() and not force:
        say(f'{dest} already exists. Remove or rename it first, or run from the menu to replace it.')
        return 2
    key = boot_key()
    say('Building the test loader (nothing is downloaded that you do not already have)')
    image = author_autoexec({'autoexec.sh': BOOT_TEST_SH}, key)
    tmp = vol / '.centrifuge-autoexec.tmp'
    tmp.write_bytes(image)
    os.replace(tmp, dest)
    say(f'\nWritten: {dest}  ({len(image) // 1024} KB)')
    say('\nTo test on the XDJ-XZ (firmware 1.26):')
    say('  1. Put this USB stick in the XZ while it is switched OFF.')
    say('  2. Switch the XZ on and wait for it to finish starting up (it boots as normal).')
    say('  3. Switch it off, take the stick out, and put it back in this PC.')
    say('  4. Look for CENTRIFUGE-BOOT-OK.txt on the stick and send it to Claude.')
    say('\nIf that file is there, our loader runs on your unit. If not, the deck still booted fine - tell Claude.')
    say('Booting WITHOUT this stick leaves the XZ completely standard.')
    return 0


def menu() -> int:
    say('Centrifuge XZ Tools')
    say('===================\n')
    say('  1. Extract the XDJ-XZ 1.26 player program (for Claude to study)')
    say('  2. Write a boot TEST loader to a USB stick (safe proof-of-life)')
    say('  3. Write a RECON loader to a USB stick (reads hardware info + knob values)')
    say('  4. Write a SPLASH loader to a USB stick (Centrifuge logo on the screen at boot)')
    say('  5. Write the SCREEN prototype to a USB stick (live Centrifuge screen on the deck)')
    say('')
    choice = input('Choose 1-5 (or press Enter to cancel): ').strip()
    if choice == '1':
        return extract()
    if choice in ('2', '3', '4', '5'):
        vol = input('USB drive to write to, e.g. E:\\  : ').strip().strip('"')
        force = False
        if vol and (Path(vol) / 'autoexec.bin').exists():
            force = input('That stick already has autoexec.bin. Replace it? (y/N): ').strip().lower() == 'y'
            if not force:
                say('Left it as it is.')
                return 0
        return {'2': testloader, '3': recon, '4': splash, '5': screen}[choice](vol, force)
    say('Cancelled.')
    return 0


def main() -> int:
    args = sys.argv[1:]
    try:
        if not args:
            return menu()
        if args[0] == 'extract':
            return extract()
        if args[0] == 'testloader' and len(args) >= 2:
            return testloader(args[1])
        if args[0] == 'recon' and len(args) >= 2:
            return recon(args[1])
        if args[0] == 'splash' and len(args) >= 2:
            return splash(args[1])
        if args[0] == 'screen' and len(args) >= 2:
            return screen(args[1])
        say(__doc__)
        return 1
    except Exception as e:  # noqa: BLE001
        say(f'\nSomething went wrong: {e}')
        return 2
    finally:
        if getattr(sys, 'frozen', False):
            input('\nPress Enter to close')


if __name__ == '__main__':
    sys.exit(main())
