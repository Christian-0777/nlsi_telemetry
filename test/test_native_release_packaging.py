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
        self.assertEqual("1.4.0", BUILDER.VERSION)
        self.assertEqual("alpha", BUILDER.CHANNEL)
        self.assertEqual("alpha", BUILDER.INSTALL_CHANNEL)
        self.assertEqual("v1.4.0-alpha", BUILDER.RELEASE_TAG)
        self.assertEqual(
            r"{autopf32}\NLSI Exclusive Logbook",
            BUILDER.default_install_dir_for_channel("alpha"),
        )
        self.assertEqual(
            r"{autopf32}\NLSI Exclusive Logbook",
            BUILDER.default_install_dir_for_channel("beta"),
        )
        self.assertEqual(
            r"{autopf64}\NLSI Exclusive Logbook",
            BUILDER.default_install_dir_for_channel("stable"),
        )
        BUILDER.verify_version()
        BUILDER.verify_installer_policy()

    def test_telemetry_migration_has_versioned_entities_and_no_fabricated_accounts(self) -> None:
        migration = (ROOT / "db" / "migrations" / "001_telemetry_sync.sql").read_text(
            encoding="utf-8"
        )
        for table in ("driving_sessions", "telemetry_samples", "job_records", "sync_state"):
            self.assertIn(f"CREATE TABLE {table}", migration)
        self.assertIn("raw_fields JSON NOT NULL", migration)
        self.assertIn("record_schema_version TINYINT UNSIGNED NOT NULL", migration)
        self.assertIn("raw_availability JSON NOT NULL", migration)
        self.assertIn("normalized_fields JSON NOT NULL", migration)
        self.assertIn("FOREIGN KEY (session_id)", migration)
        self.assertIn("received_at_utc DATETIME(3)", migration)
        self.assertNotRegex(migration, r"(?im)^\s*CREATE TABLE\s+(users|accounts)\b")

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
