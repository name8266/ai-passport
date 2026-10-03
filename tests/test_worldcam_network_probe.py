import importlib.util
from io import BytesIO
from pathlib import Path
import unittest
from unittest.mock import patch
import urllib.error

from PIL import Image

spec = importlib.util.spec_from_file_location('worldcam_network_probe',
       Path(__file__).resolve().parents[1] / 'tools/probe_worldcam_network.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class Response(BytesIO):
    status = 200

    def geturl(self):
        return 'https://camera.example/current.jpg'


class OfflineOpener:
    def __init__(self, payload):
        self.payload = payload

    def open(self, *args, **kwargs):
        if isinstance(self.payload, Exception):
            raise self.payload
        return Response(self.payload)


class ProbeTests(unittest.TestCase):
    camera = {'id': 'test', 'source': 'Test Publisher', 'snapshot': 'https://camera.example/current.jpg'}

    def test_real_decode_and_malformed_payload(self):
        buffer = BytesIO()
        Image.new('RGB', (20, 10), 'red').save(buffer, 'PNG')
        result = probe.probe_camera(self.camera, opener=OfflineOpener(buffer.getvalue()))
        self.assertEqual((result['status'], result['width'], result['height']), ('image_decoded', 20, 10))
        for malformed in [b'<html>Access denied</html>', buffer.getvalue()[:45]]:
            result = probe.probe_camera(self.camera, opener=OfflineOpener(malformed))
            self.assertEqual(result['status'], 'image_decode_error')
            self.assertEqual(result['http_status'], 200)

    def test_failures_preserve_http_code_without_private_error_text(self):
        error = urllib.error.HTTPError(self.camera['snapshot'], 403, 'Private proxy credentials', {}, None)
        result = probe.probe_camera(self.camera, opener=OfflineOpener(error))
        self.assertEqual((result['status'], result['http_status']), ('http_error', 403))
        self.assertNotIn('Private proxy credentials', str(result))
        result = probe.probe_camera(self.camera, opener=OfflineOpener(urllib.error.URLError(TimeoutError())))
        self.assertEqual(result['status'], 'timeout')

    def test_report_uses_new_results_and_never_confirms_vantage(self):
        coverage = [{'camera_ids': ['test']}, {'camera_ids': ['test', 'other']}, {'camera_ids': []}]
        with patch.dict('os.environ', {'HTTPS_PROXY': 'https://user:secret@private.example'}):
            report = probe.build_report([self.camera], [{'id': 'test', 'provider': 'Test Publisher',
                  'status': 'image_decoded'}], coverage, 'start', vantage_label='大陆家庭网络')
        self.assertEqual(report['passed'], 1)
        self.assertEqual(report['capitals']['with_decoded_image'], 2)
        self.assertEqual(report['capitals']['without_catalogued_source'], 1)
        self.assertFalse(report['vantage']['verified'])
        self.assertTrue(report['proxy_environment_configured'])
        self.assertNotIn('secret', str(report))
        self.assertEqual(report['providers']['Test Publisher']['passed'], 1)

    def test_application_proxy_mode_is_explicit(self):
        with patch.object(probe.urllib.request, 'build_opener', return_value=OfflineOpener(b'not an image')) as build:
            probe.probe_camera(self.camera)
        self.assertEqual(build.call_args.args[0].proxies, {})
        for invalid in ['../../private.jpg,Live,Live', 'https://evil.example/image.jpg,Live,Live']:
            with self.assertRaises(ValueError):
                probe.resolve_usap(invalid)


if __name__ == '__main__':
    unittest.main()
