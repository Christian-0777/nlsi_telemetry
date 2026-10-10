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
        self.assertEqual("1.5.0", BUILDER.VERSION)
        self.assertEqual("beta", BUILDER.CHANNEL)
        self.assertEqual("beta", BUILDER.INSTALL_CHANNEL)
        self.assertEqual("v1.5.0-beta", BUILDER.RELEASE_TAG)
        self.assertIn("Qt6Concurrent.dll", BUILDER.REQUIRED_RUNTIME_FILES)
        self.assertEqual(
            r"C:\Program Files\NLSI Exclusive Logbook",
            BUILDER.default_install_dir_for_channel("alpha"),
        )
        self.assertEqual(
            r"C:\Program Files\NLSI Exclusive Logbook",
            BUILDER.default_install_dir_for_channel("beta"),
        )
        self.assertEqual(
            r"C:\Program Files\NLSI Exclusive Logbook",
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

    def test_scs_position_plugins_are_version_scoped_and_architecture_checked(self) -> None:
        self.assertEqual(2, len(BUILDER.SCS_POSITION_PLUGIN_FILES))
        self.assertEqual(
            [
                ROOT / "build" / "plugins" / "v1.5.0-beta" / "win_x64" / "nlsi.dll",
                ROOT / "build" / "plugins" / "v1.5.0-beta" / "win_x86" / "nlsi.dll",
            ],
            [source for source, _, _ in BUILDER.SCS_POSITION_PLUGIN_FILES],
        )
        self.assertEqual(
            [0x8664, 0x014C],
            [machine for _, _, machine in BUILDER.SCS_POSITION_PLUGIN_FILES],
        )
        self.assertTrue(all(
            destination.name == "nlsi.dll"
            for _, destination, _ in BUILDER.SCS_POSITION_PLUGIN_FILES
        ))
        installer_text = (ROOT / "installer" / "NLSI-Exclusive-Logbook.iss").read_text(
            encoding="utf-8"
        )
        self.assertIn("InstallScsPositionPlugin.ps1", installer_text)
        self.assertIn("-RestoreManagedPlugin", installer_text)

    def test_upgrade_paths_and_per_user_data_are_preserved(self) -> None:
        installer_text = (ROOT / "installer" / "NLSI-Exclusive-Logbook.iss").read_text(
            encoding="utf-8"
        )
        app_source = (ROOT / "native" / "app" / "App.cpp").read_text(encoding="utf-8")
        self.assertIn(
            r'DefaultApplicationDir "C:\Program Files\NLSI Exclusive Logbook"',
            installer_text,
        )
        self.assertIn("UsePreviousAppDir=yes", installer_text)
        self.assertIn("AppId=NLSI Exclusive Logbook", installer_text)
        self.assertNotIn("[UninstallDelete]", installer_text)
        self.assertNotIn("DelTree(", installer_text)
        self.assertIn("QStandardPaths::AppLocalDataLocation", app_source)
        self.assertIn('setOrganizationName(QStringLiteral("NLSI"))', app_source)
        self.assertIn('setApplicationName(QStringLiteral("Exclusive Logbook"))', app_source)

    def test_scs_plugin_subscribes_only_to_player_world_placement(self) -> None:
        plugin_source = (ROOT / "scs_position_plugin" / "nlsi.cpp").read_text(
            encoding="utf-8"
        )
        self.assertEqual(1, plugin_source.count("register_for_channel("))
        self.assertIn("SCS_TELEMETRY_TRUCK_CHANNEL_world_placement", plugin_source)
        self.assertIn("SCS_VALUE_TYPE_dplacement", plugin_source)
        self.assertNotIn("register_for_event(", plugin_source)
        self.assertNotIn("TSGPSTelemetry", plugin_source)

    def test_bundled_lucide_icons_have_a_local_license_notice(self) -> None:
        license_text = (ROOT / "assets" / "icons" / "LICENSE.txt").read_text(
            encoding="utf-8"
        )
        builder_source = BUILDER_PATH.read_text(encoding="utf-8")
        self.assertIn("Lucide Contributors", license_text)
        self.assertIn("ISC License", license_text)
        self.assertIn('"assets" / "icons" / "LICENSE.txt"', builder_source)
        self.assertIn('"Lucide-ISC.txt"', builder_source)


if __name__ == "__main__":
    unittest.main()
