#!/usr/bin/env python3
"""Generate a build-only Wi-Fi header; never print credentials or put them in argv."""
import argparse
import json
import os
from pathlib import Path


def c_bytes(value):
    return '"' + ''.join('\\%03o' % b for b in value.encode('utf-8')) + '"'


def generate(output, repository, credentials=None):
    ssid = password = ''
    if credentials is not None:
        private = credentials.resolve(strict=True)
        if private.is_relative_to(repository.resolve()):
            raise ValueError('Wi-Fi configuration must be outside the repository')
        if os.name == 'posix' and private.stat().st_mode & 0o077:
            raise ValueError('Wi-Fi configuration must have private file permissions (0600)')
        data = json.loads(private.read_text(encoding='utf-8'))
        if not isinstance(data, dict) or set(data) != {'ssid', 'password'}:
            raise ValueError('Wi-Fi configuration requires only ssid and password fields')
        ssid, password = data['ssid'], data['password']
        if not isinstance(ssid, str) or not isinstance(password, str):
            raise ValueError('Wi-Fi fields must be strings')
        if '\0' in ssid or '\0' in password:
            raise ValueError('Wi-Fi fields cannot contain NUL')
        if not 1 <= len(ssid.encode('utf-8')) <= 32:
            raise ValueError('Wi-Fi SSID must contain 1–32 UTF-8 bytes')
        length = len(password.encode('utf-8'))
        if length != 0 and not 8 <= length <= 63:
            raise ValueError('Wi-Fi password must be empty or contain 8–63 UTF-8 bytes')
    body = ('/* Generated in the build directory. Private builds must not be published. */\n'
            '#pragma once\n'
            '#define STOCK_WIFI_BUILTIN_SSID ' + c_bytes(ssid) + '\n'
            '#define STOCK_WIFI_BUILTIN_PASSWORD ' + c_bytes(password) + '\n')
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() and output.read_text(encoding='ascii') == body:
        if os.name == 'posix':
            output.chmod(0o600)
        return
    # An existing generated header must also lose permissive permissions.
    fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, 'w', encoding='ascii') as file:
        if os.name == 'posix':
            os.fchmod(file.fileno(), 0o600)
        file.write(body)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repository', type=Path, required=True)
    source = parser.add_mutually_exclusive_group()
    source.add_argument('--credentials-file', type=Path)
    source.add_argument('--from-environment', action='store_true')
    args = parser.parse_args()
    try:
        private = args.credentials_file
        if args.from_environment:
            value = os.environ.get('STOCK_WIFI_CREDENTIALS_FILE', '')
            private = Path(value) if value else None
        generate(args.output, args.repository, private)
    except (OSError, ValueError, UnicodeError):
        # JSON errors and paths can reveal secrets, so keep diagnostics generic.
        parser.exit(1, 'Invalid private Wi-Fi configuration; check path, permissions and field lengths.\n')
