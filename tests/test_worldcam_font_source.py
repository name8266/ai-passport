import hashlib
import importlib.util
from io import BytesIO
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('font_source',
    Path(__file__).resolve().parents[1] / 'tools/fetch_worldcam_font_source.py')
source = importlib.util.module_from_spec(spec)
spec.loader.exec_module(source)


class SourceIntegrityTests(unittest.TestCase):
    def test_download_must_match_pinned_bytes(self):
        data = b'font test fixture'; digest = hashlib.sha256(data).hexdigest()
        with tempfile.TemporaryDirectory() as folder, patch.object(source, 'SHA256', digest):
            target = Path(folder) / 'font.otf'
            source.fetch_font(target, opener=lambda *args, **kwargs: BytesIO(data))
            self.assertEqual(target.read_bytes(), data)
            source.fetch_font(target, opener=lambda *args, **kwargs: self.fail('Unexpected download'))

    def test_existing_different_font_is_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / 'font.otf'; target.write_bytes(b'user source')
            with self.assertRaises(ValueError):
                source.fetch_font(target, opener=lambda *args, **kwargs: self.fail('Unexpected download'))
            self.assertEqual(target.read_bytes(), b'user source')

    def test_bad_or_oversized_download_leaves_no_file(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / 'font.otf'
            with self.assertRaises(ValueError):
                source.fetch_font(target, opener=lambda *args, **kwargs: BytesIO(b'bad bytes'))
            self.assertEqual(list(Path(folder).iterdir()), [])
            with patch.object(source, 'MAX_BYTES', 1), self.assertRaises(ValueError):
                source.fetch_font(target, opener=lambda *args, **kwargs: BytesIO(b'xx'))
            self.assertEqual(list(Path(folder).iterdir()), [])


if __name__ == '__main__':
    unittest.main()
