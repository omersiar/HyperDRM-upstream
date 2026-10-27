"""Version drift must fail before an inconsistent package can be built."""
import json
from pathlib import Path
import tempfile
import unittest

import sync_version


class VersionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.previous_root = sync_version.ROOT
        sync_version.ROOT = Path(self.directory.name)
        for name in ('app', 'package', 'native'):
            (sync_version.ROOT / name).mkdir()
        (sync_version.ROOT / 'VERSION').write_text('1.2.3\n')
        for name in ('app/appinfo.json', 'package/packageinfo.json'):
            (sync_version.ROOT / name).write_text(json.dumps({'version': '1.0.0', 'id': 'unchanged'}))

    def tearDown(self):
        sync_version.ROOT = self.previous_root
        self.directory.cleanup()

    def test_check_rejects_drift_without_writing(self):
        with self.assertRaises(ValueError):
            sync_version.synchronize(check=True)
        self.assertFalse((sync_version.ROOT / 'native/version.h').exists())

    def test_sync_preserves_metadata_and_produces_runtime_version(self):
        self.assertEqual(sync_version.synchronize(), '1.2.3')
        sync_version.synchronize(check=True)
        app = json.loads((sync_version.ROOT / 'app/appinfo.json').read_text())
        self.assertEqual(app, {'version': '1.2.3', 'id': 'unchanged'})
        self.assertIn('"1.2.3"', (sync_version.ROOT / 'native/version.h').read_text())

    def test_ui_drift_is_rejected(self):
        sync_version.synchronize()
        (sync_version.ROOT / 'app/version.js').write_text("var HYPERDRM_APP_VERSION = 'old';")
        with self.assertRaises(ValueError):
            sync_version.synchronize(check=True)

    def test_invalid_version_is_rejected(self):
        (sync_version.ROOT / 'VERSION').write_text('1.2\n')
        with self.assertRaises(ValueError):
            sync_version.synchronize()

    def test_native_drift_is_rejected(self):
        sync_version.synchronize()
        (sync_version.ROOT / 'native/version.h').write_text('old version')
        with self.assertRaises(ValueError):
            sync_version.synchronize(check=True)


if __name__ == '__main__':
    unittest.main()
