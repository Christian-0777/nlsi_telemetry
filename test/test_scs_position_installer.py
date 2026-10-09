import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INSTALLER_SCRIPT = ROOT / "installer" / "InstallScsPositionPlugin.ps1"
POWERSHELL = shutil.which("powershell.exe")


@unittest.skipUnless(POWERSHELL, "Windows PowerShell is required for installer integration tests")
class ScsPositionInstallerTests(unittest.TestCase):
    def invoke_installer(
        self,
        app_root: Path,
        library_root: Path,
        dll64: Path,
        dll32: Path,
        restore: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        command = [
            POWERSHELL,
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(INSTALLER_SCRIPT),
            "-DllPath64",
            str(dll64),
            "-DllPath32",
            str(dll32),
            "-ManifestPath",
            str(app_root / "tools" / "installed-scs-position-plugins.json"),
            "-BackupDirectory",
            str(app_root / "plugin-backups" / "scs-position"),
            "-StatusPath",
            str(app_root / "logs" / "scs-position-plugin-status.txt"),
            "-SteamLibraryRoots",
            str(library_root),
        ]
        if restore:
            command.append("-RestoreManagedPlugin")
        return subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )

    def test_missing_game_is_reported_without_creating_plugin_folders(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app_root = root / "app"
            library_root = root / "SteamLibrary"
            dll64 = root / "nlsi-x64.dll"
            dll32 = root / "nlsi-x86.dll"
            library_root.mkdir()
            dll64.write_bytes(b"x64 plugin")
            dll32.write_bytes(b"x86 plugin")

            result = self.invoke_installer(app_root, library_root, dll64, dll32)

            self.assertEqual(0, result.returncode, result.stderr)
            self.assertIn(
                "no supported Steam ETS2/ATS game architecture",
                (app_root / "logs" / "scs-position-plugin-status.txt").read_text(
                    encoding="utf-8-sig"
                ),
            )
            self.assertFalse((library_root / "steamapps" / "common").exists())

    def test_missing_plugin_source_fails_explicitly(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library_root = root / "SteamLibrary"
            library_root.mkdir()
            app_root = root / "app"
            missing = root / "missing.dll"

            result = self.invoke_installer(app_root, library_root, missing, missing)

            self.assertNotEqual(0, result.returncode)
            self.assertFalse((app_root / "tools" / "installed-scs-position-plugins.json").exists())

    def test_existing_nlsi_plugin_is_backed_up_then_restored(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app_root = root / "app"
            library_root = root / "SteamLibrary"
            game_root = library_root / "steamapps" / "common" / "Euro Truck Simulator 2"
            plugin_x64 = game_root / "bin" / "win_x64" / "plugins" / "nlsi.dll"
            plugin_x86 = game_root / "bin" / "win_x86" / "plugins" / "nlsi.dll"
            plugin_x64.parent.mkdir(parents=True)
            plugin_x86.parent.mkdir(parents=True)
            plugin_x64.write_bytes(b"preexisting x64 nlsi")
            plugin_x86.write_bytes(b"preexisting x86 nlsi")
            dll64 = root / "new-x64.dll"
            dll32 = root / "new-x86.dll"
            dll64.write_bytes(b"new x64 nlsi")
            dll32.write_bytes(b"new x86 nlsi")

            installed = self.invoke_installer(app_root, library_root, dll64, dll32)

            self.assertEqual(0, installed.returncode, installed.stderr)
            self.assertEqual(b"new x64 nlsi", plugin_x64.read_bytes())
            self.assertEqual(b"new x86 nlsi", plugin_x86.read_bytes())
            manifest = (app_root / "tools" / "installed-scs-position-plugins.json").read_text(
                encoding="utf-8-sig"
            )
            self.assertIn("backup_path", manifest)

            restored = self.invoke_installer(
                app_root, library_root, dll64, dll32, restore=True
            )

            self.assertEqual(0, restored.returncode, restored.stderr)
            self.assertEqual(b"preexisting x64 nlsi", plugin_x64.read_bytes())
            self.assertEqual(b"preexisting x86 nlsi", plugin_x86.read_bytes())
            self.assertFalse(
                (app_root / "tools" / "installed-scs-position-plugins.json").exists()
            )


if __name__ == "__main__":
    unittest.main()
