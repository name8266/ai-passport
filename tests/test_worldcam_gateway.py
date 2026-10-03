import importlib.util
from io import BytesIO
from pathlib import Path
import struct
import threading
import unittest
from unittest.mock import patch
import urllib.error
import urllib.request
from PIL import Image

spec = importlib.util.spec_from_file_location('worldcam_gateway', Path(__file__).resolve().parents[1] / 'gateway/server.py')
gateway = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gateway)

class GatewayTests(unittest.TestCase):
    def test_search(self):
        self.assertEqual(gateway.catalogue('香港')[0]['id'], 'hongkong')
        self.assertTrue(any(c['country']=='Germany' for c in gateway.catalogue('GERMANY')))
        self.assertEqual(gateway.catalogue('香港', 'Europe'), [])
        self.assertEqual(gateway.catalogue('does not exist'), [])
        self.assertTrue(all('snapshot' not in c for c in gateway.catalogue()))

    def test_publisher_image_resolution(self):
        self.assertEqual(gateway.resolve_usap('spole00036.jpg?=123,Live,Live'),
                         'https://www.usap.gov/videoClipsAndMaps/SouthPoleWebcam/spole00036.jpg')
        for value in ['../../private.jpg,Live,Live', 'https://evil.example/image.jpg,Live,Live', 'invalid']:
            with self.assertRaises(ValueError): gateway.resolve_usap(value)

    def test_wire_color_and_dimensions(self):
        frame = gateway.encode_frame(Image.new('RGB', (192,128), (255,0,0)), 12345)
        self.assertEqual(len(frame), 49164)
        self.assertEqual(struct.unpack('<4sHHI', frame[:12]), (b'WCAM',192,128,12345))
        self.assertEqual(struct.unpack('<H', frame[12:14])[0], 0xf800)

    def test_fullscreen_frame(self):
        frame=gateway.encode_frame(Image.new('RGB',(240,160),(0,255,0)),77,full=True)
        self.assertEqual(len(frame),76812)
        self.assertEqual(struct.unpack('<4sHHI',frame[:12]),(b'WCAM',240,160,77))
        self.assertEqual(struct.unpack('<H',frame[12:14])[0],0x07e0)

    def test_catalogue_scale_capitals_and_index(self):
        self.assertGreaterEqual(len(gateway.CAMERAS),300)
        self.assertEqual(len(gateway.COVERAGE),195)
        self.assertEqual(len({c['country_code'] for c in gateway.COVERAGE}),195)
        data=gateway.index_binary()
        self.assertEqual(struct.unpack('<4sHH',data[:8]),(b'WCIX',len(gateway.LOCATIONS),8))
        self.assertEqual(len(data),8+len(gateway.LOCATIONS)*8)
        for i in range(len(gateway.LOCATIONS)):
            index,lat,lon,flags,reserved=struct.unpack('<HhhBB',data[8+i*8:16+i*8])
            self.assertEqual(index,i);self.assertTrue(-9000<=lat<=9000);self.assertTrue(-18000<=lon<=18000);self.assertEqual(reserved,0)
            if flags & 1:self.assertIsNotNone(gateway.LOCATIONS[i]['camera_id'])
        gaps=[c for c in gateway.COVERAGE if not c['camera_ids']]
        self.assertTrue(gaps)
        for capital in gaps:
            location=next(l for l in gateway.LOCATIONS if l['id']=='capital-'+capital['country_code'].lower())
            self.assertIsNone(location['camera_id'])
            self.assertFalse(gateway.location_info(location['index'])['available'])
        self.assertEqual(len(gateway.locations(capitals=True)),195)

    def test_http_failure_and_unknown_sources(self):
        server = gateway.Server(('127.0.0.1',0), gateway.Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f'http://127.0.0.1:{server.server_port}'
        try:
            with urllib.request.urlopen(base+'/api/cameras?region=Polar') as r:
                self.assertIn(b'southpole', r.read())
            with self.assertRaises(urllib.error.HTTPError) as error:
                urllib.request.urlopen(base+'/api/frame/unknown')
            self.assertEqual(error.exception.code, 404)
            with patch.object(gateway, 'fetch_frame', side_effect=OSError('offline')):
                with self.assertRaises(urllib.error.HTTPError) as error:
                    urllib.request.urlopen(base+'/api/frame/apgar')
                self.assertEqual(error.exception.code, 502)
        finally:
            server.shutdown(); server.server_close(); thread.join()

if __name__ == '__main__': unittest.main()
