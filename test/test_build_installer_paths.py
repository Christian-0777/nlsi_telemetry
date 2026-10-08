import importlib.util
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parents[1] / "installer" / "build_installer.py"
SPEC = importlib.util.spec_from_file_location("nlsi_build_installer", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(MODULE)
BATCH_PATH = Path(__file__).resolve().parents[1] / "build-installer.bat"


class BuildInstallerPathTests(unittest.TestCase):
    def _run_launcher_with_python_stub(
        self
    ) -> tuple[int, str, list[str], Path]:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            batch_root = temp_root / "project"
            (batch_root / "installer").mkdir(parents=True)
            (batch_root / "installer" / "build_native_installer.py").write_text("", encoding="utf-8")
            batch_path = batch_root / "build-installer.bat"
            batch_path.write_bytes(BATCH_PATH.read_bytes())
            argument_log = temp_root / "python-arguments.txt"
            python_stub = temp_root / "py.cmd"
            python_stub.write_text(
                '@echo off\r\n'
                '> "%TEST_ARGUMENT_LOG%" echo(%1\r\n'
                '>> "%TEST_ARGUMENT_LOG%" echo(%~2\r\n'
                '>> "%TEST_ARGUMENT_LOG%" echo(%~3\r\n',
                encoding="utf-8",
            )
            environment = os.environ.copy()
            environment["PATH"] = f"{temp_root};{environment['PATH']}"
            environment["TEST_ARGUMENT_LOG"] = str(argument_log)
            command = ["cmd.exe", "/d", "/c", str(batch_path)]
            result = subprocess.run(
                command,
                capture_output=True,
                text=True,
                env=environment,
                check=False,
            )
            arguments = argument_log.read_text(encoding="utf-8").splitlines() if argument_log.exists() else []
            return result.returncode, result.stdout + result.stderr, arguments, batch_root

    @unittest.skipUnless(os.name == "nt", "The installer launcher is a Windows batch file.")
    def test_launcher_invokes_native_installer_builder(self) -> None:
        return_code, output, arguments, batch_root = self._run_launcher_with_python_stub()
        self.assertEqual(0, return_code, output)
        self.assertEqual(
            [
                "-3",
                str(batch_root / "installer" / "build_native_installer.py"),
                "",
            ],
            arguments,
        )

    def test_versioned_build_path_uses_version_directory(self) -> None:
        path = MODULE.versioned_build_path("0.3.2")
        self.assertEqual(Path(__file__).resolve().parents[1] / "build" / "v0.3.2", path)

    def test_prepare_staging_prefers_versioned_dll(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            stage_root = repo_root / "installer" / "staging"
            build_root = repo_root / "build"
            version_root = build_root / "v0.3.2"
            version_root.mkdir(parents=True)
            (version_root / "nlsi_telemetry.dll").write_bytes(b"versioned")
            (build_root / "nlsi_telemetry.dll").write_bytes(b"fallback")
            (repo_root / "agent.py").write_text("print('agent')\n", encoding="utf-8")
            (repo_root / "README.md").write_text("readme\n", encoding="utf-8")
            (repo_root / "version.json").write_text('{"version": "0.3.2"}\n', encoding="utf-8")
            (repo_root / ".env.example").write_text("ENV\n", encoding="utf-8")
            (repo_root / "installer").mkdir(parents=True, exist_ok=True)
            (repo_root / "gui_app.py").write_text("GUI\n", encoding="utf-8")
            (repo_root / "img").mkdir()
            (repo_root / "img" / "logo.ico").write_bytes(b"icon")
            (repo_root / "installer" / "NLSI-Telemetry-Launcher.bat").write_text("@echo off\n", encoding="utf-8")
            (repo_root / "installer" / "InstallGamePlugins.ps1").write_text("Write-Output 'ok'\n", encoding="utf-8")

            original_root = MODULE.ROOT
            try:
                MODULE.ROOT = repo_root
                MODULE.prepare_staging("0.3.2", repo_root)
                self.assertEqual(b"versioned", (stage_root / "bin" / "nlsi_telemetry.dll").read_bytes())
                self.assertEqual("GUI\n", (stage_root / "app" / "gui_app.py").read_text(encoding="utf-8"))
                self.assertEqual(b"icon", (stage_root / "app" / "img" / "logo.ico").read_bytes())
            finally:
                MODULE.ROOT = original_root

    def test_prepare_staging_rejects_shared_root_dll(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            build_root = repo_root / "build"
            build_root.mkdir(parents=True)
            (build_root / "nlsi_telemetry.dll").write_bytes(b"fallback")
            (repo_root / "agent.py").write_text("print('agent')\n", encoding="utf-8")
            (repo_root / "README.md").write_text("readme\n", encoding="utf-8")
            (repo_root / "version.json").write_text('{"version": "0.3.2"}\n', encoding="utf-8")
            (repo_root / ".env.example").write_text("ENV\n", encoding="utf-8")
            (repo_root / "installer").mkdir(parents=True, exist_ok=True)
            (repo_root / "installer" / "NLSI-Telemetry-Launcher.bat").write_text("@echo off\n", encoding="utf-8")
            (repo_root / "installer" / "InstallGamePlugins.ps1").write_text("Write-Output 'ok'\n", encoding="utf-8")

            original_root = MODULE.ROOT
            try:
                MODULE.ROOT = repo_root
                with self.assertRaisesRegex(FileNotFoundError, "v0.3.2"):
                    MODULE.prepare_staging("0.3.2", repo_root)
            finally:
                MODULE.ROOT = original_root


if __name__ == "__main__":
    unittest.main()
