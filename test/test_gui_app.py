import json
import sys
import tempfile
import tkinter as tk
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import agent as agent_module
from agent import TelemetryAgent, app_channel, app_product, app_release_label, app_version
from gui_app import (
    NLSITelemetryApp,
    TAB_NAMES,
    filter_jobs,
    jobs_from_events,
    make_snapshot,
    parse_env_urls,
    read_event_records,
    redact_sensitive,
)


class GuiDataTests(unittest.TestCase):
    def test_application_version_is_separate_from_telemetry_api_version(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            agent = TelemetryAgent(Path(temp_dir), output=lambda _message: None)
            agent.process_message(
                {
                    "type": "plugin_init",
                    "game": {"id": "ets2", "name": "ETS2", "version": "1.0", "telemetry_api_version": "1.01"},
                    "timestamp": "2026-10-08T00:00:00Z",
                },
                now=1.0,
            )
            snapshot = make_snapshot(agent, None, "Listening")
            self.assertEqual("1.4.2", app_version())
            self.assertEqual("NLSI Exclusive Logbook", app_product())
            self.assertEqual("beta", app_channel())
            self.assertEqual("Beta v1.4.2", app_release_label())
            self.assertEqual("1.4.2", snapshot["application_version"])
            self.assertEqual("1.01", snapshot["telemetry_api_version"])
            agent.close()

    def test_snapshot_without_game_data_is_safe_and_does_not_invent_values(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            agent = TelemetryAgent(Path(temp_dir), output=lambda _message: None)
            snapshot = make_snapshot(agent, None, "Listening")
            self.assertEqual("Idle", snapshot["state"])
            self.assertEqual("--", snapshot["session_id"])
            self.assertEqual({}, snapshot["latest_telemetry"])
            self.assertIsNone(snapshot["game"].get("telemetry_api_version"))

    def test_version_is_resolved_from_app_directory_or_install_root(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            app_dir = temp_root / "app"
            app_dir.mkdir()
            (temp_root / "version.json").write_text('{"version": "9.9.9"}\n', encoding="utf-8")
            with mock.patch.object(agent_module, "ROOT", app_dir):
                self.assertEqual(temp_root / "version.json", agent_module._version_path())
                self.assertEqual("9.9.9", agent_module.app_version())

    def test_jobs_are_derived_from_persisted_events_and_status_filter(self) -> None:
        records = [
            {
                "timestamp": "2026-10-08T00:00:00Z",
                "event": "JOB_STARTED",
                "game": "ets2",
                "data": {
                    "session_id": "NLSI-20261008-0001",
                    "job": {"cargo": "Furniture", "source": "Berlin", "destination": "Paris", "planned_distance_km": 500},
                },
            },
            {
                "timestamp": "2026-10-08T01:00:00Z",
                "event": "job.delivered",
                "game": "ets2",
                "data": {
                    "session_id": "NLSI-20261008-0001",
                    "distance_km": 495,
                    "job_snapshot": {"cargo": "Furniture", "source": "Berlin", "destination": "Paris"},
                },
            },
            {
                "timestamp": "2026-10-08T02:00:00Z",
                "event": "JOB_STARTED",
                "game": "ats",
                "data": {"session_id": "NLSI-20261008-0002", "job": {"cargo": "Logs"}},
            },
        ]
        jobs = jobs_from_events(records)
        self.assertEqual(["Delivered", "Pending"], [job["status"] for job in jobs])
        self.assertEqual(["Berlin", "Paris"], [jobs[0]["details"]["source"], jobs[0]["details"]["destination"]])
        self.assertEqual([jobs[0]], filter_jobs(jobs, "Delivered"))
        self.assertEqual(jobs, filter_jobs(jobs, "All"))

    def test_events_load_from_jsonl(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "events.jsonl"
            expected = [{"event": "SESSION_STARTED", "data": {"session_id": "session"}}]
            path.write_text(json.dumps(expected[0]) + "\n", encoding="utf-8")
            self.assertEqual(expected, read_event_records(path))

    def test_only_social_urls_are_read_from_env_and_secret_values_are_redacted(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            env_path = Path(temp_dir) / ".env"
            env_path.write_text(
                "NLSI_API_KEY=super-secret-value\nSOCIAL_DISCORD_URL=https://example.test/community\n",
                encoding="utf-8",
            )
            values = parse_env_urls((env_path,))
            self.assertEqual({"SOCIAL_DISCORD_URL": "https://example.test/community"}, values)
            self.assertNotIn("super-secret-value", json.dumps(values))
            self.assertEqual(
                {
                    "APIKey": "[REDACTED]",
                    "Authorization": "[REDACTED]",
                    "nested": {"password": "[REDACTED]", "safe": "value"},
                },
                redact_sensitive(
                    {
                        "APIKey": "secret",
                        "Authorization": "bearer secret",
                        "nested": {"password": "secret", "safe": "value"},
                    }
                ),
            )

    def test_delivering_job_does_not_create_pdf_without_user_export(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir)
            agent = TelemetryAgent(output_dir, output=lambda _message: None)
            game = {"id": "ets2", "name": "ETS2", "version": "1.0", "telemetry_api_version": "1.01"}
            agent.process_message({"type": "plugin_init", "game": game}, now=1.0)
            agent.process_message(
                {"type": "gameplay_event", "game": game, "event": "job.delivered", "data": {"distance_km": 12}},
                now=2.0,
            )
            self.assertEqual(["events.jsonl"], sorted(path.name for path in output_dir.iterdir()))
            agent._write_pdf_report({"session_id": "session", "status": "Delivered"}, output_dir / "manual.pdf")
            self.assertTrue((output_dir / "manual.pdf").is_file())

    def test_all_seven_tabs_initialize_without_live_telemetry(self) -> None:
        root = None
        app = None
        try:
            root = tk.Tk()
            root.withdraw()
            app = NLSITelemetryApp(root, port=0)
            time.sleep(0.2)
            root.update()
            names = tuple(app.notebook.tab(tab_id, "text") for tab_id in app.notebook.tabs())
            self.assertEqual(TAB_NAMES, names)
            self.assertEqual("canvas", app.main_canvas.winfo_class().lower())
            self.assertEqual(app.main_tab, app.main_canvas.master)
            self.assertEqual(app.main_tab, app.main_scrollbar.master)
            self.assertEqual("vertical", str(app.main_scrollbar.cget("orient")))
            self.assertEqual("canvas", app.main_content.master.winfo_class().lower())
            self.assertEqual(str(app.main_content), app.main_canvas.itemcget(app.main_content_window, "window"))
            self.assertEqual("Idle", app.snapshot["state"])
            app.close()
            root = None
        except tk.TclError as error:
            self.skipTest(f"Tk display unavailable: {error}")
        finally:
            if root is not None:
                if app is not None:
                    app.close()
                else:
                    root.destroy()


if __name__ == "__main__":
    unittest.main()
