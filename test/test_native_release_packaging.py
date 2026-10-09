import hashlib
import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILDER_PATH = ROOT / "installer" / "build_native_installer.py"
SPEC = importlib.util.spec_from_file_location("nlsi_native_release_builder", BUILDER_PATH)
BUILDER = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(BUILDER)


class NativeReleasePackagingTests(unittest.TestCase):
    def test_version_and_automatic_plugin_installation_policy(self) -> None:
        self.assertEqual("1.3.8", BUILDER.VERSION)
        self.assertEqual("alpha", BUILDER.CHANNEL)
        BUILDER.verify_version()
        BUILDER.verify_installer_policy()

    def test_only_hash_verified_official_plugin_architectures_are_packaged(self) -> None:
        self.assertEqual(2, len(BUILDER.TRUCKSIM_PLUGIN_FILES))
        for source, expected_hash, destination in BUILDER.TRUCKSIM_PLUGIN_FILES:
            self.assertTrue(source.is_file(), str(source))
            self.assertEqual(
                expected_hash,
                hashlib.sha256(source.read_bytes()).hexdigest().upper(),
            )
            self.assertIn(destination.parts[2], {"win_x64", "win_x86"})
            self.assertEqual("trucksim-gps-telemetry.dll", destination.name)
        self.assertFalse(any("server" in item.name.lower() for item in BUILDER.RELEASE_DIR.glob("*.exe")))


if __name__ == "__main__":
    unittest.main()
