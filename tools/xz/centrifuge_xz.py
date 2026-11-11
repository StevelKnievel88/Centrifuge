"""Centrifuge XZ tools (Windows). First job: get the XDJ-XZ 1.26 player program out of the official firmware so its
mixer / EQ handling can be studied for the Centrifuge loader.

  centrifuge-xz.exe extract            downloads the official 1.26 firmware (AlphaTheta) and Pioneer's published
                                       source archive (for the boot key), checks both, and writes
                                       "Centrifuge XZ\\xz-1.26-player.zip" on the desktop (the player program files)

Nothing is written to the XDJ-XZ or to any USB stick. Downloads are cached in %LOCALAPPDATA%\\Centrifuge\\xz.

The download / boot-support / decryption steps follow the MIT-licensed XZ Mods builder by OpticMystic
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


def main() -> int:
    command = sys.argv[1] if len(sys.argv) > 1 else 'extract'
    try:
        if command == 'extract':
            return extract()
        say(__doc__)
        return 1
    except Exception as e:  # noqa: BLE001
        say(f'\nSomething went wrong: {e}')
        return 2
    finally:
        if getattr(sys, 'frozen', False) and len(sys.argv) <= 1:
            input('\nPress Enter to close')


if __name__ == '__main__':
    sys.exit(main())
