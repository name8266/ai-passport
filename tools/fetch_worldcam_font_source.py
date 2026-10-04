#!/usr/bin/env python3
"""Fetch the optional original CJK font from a pinned upstream commit."""
import hashlib
from pathlib import Path
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
COMMIT = 'f8d157532fbfaeda587e826d4cd5b21a49186f7c'
URL = f'https://raw.githubusercontent.com/notofonts/noto-cjk/{COMMIT}/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf'
SHA256 = '2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b'
MAX_BYTES = 24 * 1024 * 1024


def fetch_font(target, opener=urllib.request.urlopen):
    target = Path(target)
    if target.exists():
        if hashlib.sha256(target.read_bytes()).hexdigest() != SHA256:
            raise ValueError('Existing CJK source differs from the pinned font; it was not replaced')
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as out:
            temporary = Path(out.name)
            digest = hashlib.sha256(); total = 0
            with opener(URL, timeout=30) as response:
                while chunk := response.read(65536):
                    total += len(chunk)
                    if total > MAX_BYTES:
                        raise ValueError('CJK font download exceeds limit')
                    digest.update(chunk); out.write(chunk)
        if digest.hexdigest() != SHA256:
            raise ValueError('CJK font download failed SHA256 verification')
        temporary.replace(target)
    finally:
        if temporary and temporary.exists():
            temporary.unlink()


if __name__ == '__main__':
    fetch_font(ROOT / 'assets/fonts/NotoSansCJKsc-Regular.otf')
    print('Pinned Noto Sans CJK source: SHA256 verified')
