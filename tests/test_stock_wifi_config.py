#!/usr/bin/env python3
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('stock_wifi_config', ROOT / 'tools/stock_wifi_config.py')
CONFIG = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CONFIG)


class PrivateWiFiTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.repo = self.root / 'repo'
        self.repo.mkdir()
        self.private = self.root / 'credentials.json'
        self.output = self.repo / 'build/stock_wifi_build.h'

    def tearDown(self):
        self.temp.cleanup()

    def write(self, ssid='test-network', password='test-password'):
        self.private.write_text(json.dumps({'ssid': ssid, 'password': password}), encoding='utf-8')
        self.private.chmod(0o600)

    def test_public_build_has_no_credentials(self):
        CONFIG.generate(self.output, self.repo)
        self.assertIn('#define STOCK_WIFI_BUILTIN_SSID ""', self.output.read_text())
        self.assertIn('#define STOCK_WIFI_BUILTIN_PASSWORD ""', self.output.read_text())

    def test_private_header_compiles_and_preserves_exact_utf8_bytes(self):
        ssid = '中文"\\\n#'
        password = 'test"\\$()123'
        self.write(ssid, password)
        CONFIG.generate(self.output, self.repo, self.private)
        if os.name == 'posix':
            self.assertEqual(self.output.stat().st_mode & 0o777, 0o600)
        source = self.root / 'verify.c'
        source.write_text('#include "stock_wifi_build.h"\n#include <stdio.h>\n'
                          'int main(void) {const char s[]=STOCK_WIFI_BUILTIN_SSID;'
                          'const char p[]=STOCK_WIFI_BUILTIN_PASSWORD;'
                          'fwrite(s,1,sizeof(s),stdout);fwrite(p,1,sizeof(p),stdout);}\n')
        executable = self.root / 'verify'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Werror',
                        '-I'+str(self.output.parent), str(source), '-o', str(executable)], check=True)
        actual = subprocess.check_output([str(executable)])
        self.assertEqual(actual, ssid.encode()+b'\0'+password.encode()+b'\0')
        # Switching the same build directory to a public build removes both secrets.
        CONFIG.generate(self.output, self.repo)
        self.assertNotIn('\\344', self.output.read_text())
        self.assertIn('#define STOCK_WIFI_BUILTIN_PASSWORD ""', self.output.read_text())

    def test_utf8_byte_limits_and_open_network(self):
        for ssid, password, valid in [('a'*32, 'p'*63, True), ('中文', '', True),
                                     ('', 'p'*8, False), ('a'*33, 'p'*8, False),
                                     ('中'*11, 'p'*8, False), ('ok', 'short', False),
                                     ('ok', 'p'*64, False), ('ok\0bad', 'p'*8, False),
                                     ('ok', 'p'*8+'\0', False)]:
            with self.subTest(valid=valid, ssid_length=len(ssid)):
                self.write(ssid, password)
                if valid:
                    CONFIG.generate(self.output, self.repo, self.private)
                else:
                    with self.assertRaises(ValueError):
                        CONFIG.generate(self.output, self.repo, self.private)

    def test_rejects_repository_file_or_public_permissions(self):
        self.write()
        inside = self.repo / 'private.json'
        inside.write_bytes(self.private.read_bytes())
        inside.chmod(0o600)
        with self.assertRaises(ValueError):
            CONFIG.generate(self.output, self.repo, inside)
        if os.name == 'posix':
            self.private.chmod(0o644)
            with self.assertRaises(ValueError):
                CONFIG.generate(self.output, self.repo, self.private)

    def test_build_uses_current_environment_on_every_invocation(self):
        self.write()
        command = ['python3', str(ROOT/'tools/stock_wifi_config.py'),
                   '--repository', str(self.repo), '--output', str(self.output),
                   '--from-environment']
        environment = os.environ.copy()
        environment['STOCK_WIFI_CREDENTIALS_FILE'] = str(self.private)
        subprocess.run(command, env=environment, check=True, capture_output=True)
        private_header = self.output.read_text()
        self.assertNotIn('#define STOCK_WIFI_BUILTIN_SSID ""', private_header)
        first_mtime = self.output.stat().st_mtime_ns
        subprocess.run(command, env=environment, check=True, capture_output=True)
        self.assertEqual(self.output.stat().st_mtime_ns, first_mtime)
        environment.pop('STOCK_WIFI_CREDENTIALS_FILE')
        subprocess.run(command, env=environment, check=True, capture_output=True)
        self.assertIn('#define STOCK_WIFI_BUILTIN_SSID ""', self.output.read_text())
        self.assertNotEqual(self.output.read_text(), private_header)

    def test_cli_error_does_not_disclose_contents(self):
        self.private.write_text('{"ssid":"PRIVATE-SENTINEL","password": bad json}')
        self.private.chmod(0o600)
        result = subprocess.run(['python3', str(ROOT/'tools/stock_wifi_config.py'),
                                 '--repository', str(self.repo), '--output', str(self.output),
                                 '--credentials-file', str(self.private)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('PRIVATE-SENTINEL', result.stderr+result.stdout)
        self.assertNotIn('bad json', result.stderr+result.stdout)


if __name__ == '__main__':
    unittest.main()
