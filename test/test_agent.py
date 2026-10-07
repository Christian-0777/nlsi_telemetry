import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from agent import TelemetryAgent


GAME = {
    "id": "ets2",
    "name": "Euro Truck Simulator 2",
    "version": "1.20",
    "telemetry_api_version": "1.01",
}


def telemetry(odometer: float, state: str = "driving") -> dict:
    return {
        "type": "telemetry",
        "timestamp": "2026-01-01T00:00:00.000Z",
        "game": GAME,
        "state": state,
        "truck": {"odometer_km": odometer, "speed_kmh": None},
        "position": {"x": None, "y": None, "z": None},
        "configurations": {},
    }


class TelemetryAgentTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.output: list[str] = []
        self.agent = TelemetryAgent(
            Path(self.temp_dir.name),
            self.output.append,
            monotonic=lambda: 0.0,
        )

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def read_events(self) -> list[dict]:
        events_path = Path(self.temp_dir.name) / "events.jsonl"
        if not events_path.exists():
            return []
        return [json.loads(line) for line in events_path.read_text(encoding="utf-8").splitlines()]

    def test_session_distance_job_count_and_shutdown(self) -> None:
        self.agent.process_message(
            {"type": "plugin_init", "game": GAME, "timestamp": "2026-01-01T00:00:00Z"},
            now=0.0,
        )
        self.agent.process_message(
            {
                "type": "configuration",
                "game": GAME,
                "id": "truck",
                "attributes": {"brand": "SCS-provided brand"},
            },
            now=0.0,
        )
        self.agent.process_message(telemetry(100.0), now=1.0)
        self.agent.process_message(telemetry(101.5), now=3.0)
        self.agent.process_message(
            {
                "type": "gameplay_event",
                "game": GAME,
                "event": "job.delivered",
                "data": {"distance.km": 150.0},
            },
            now=4.0,
        )
        self.agent.process_message(
            {"type": "plugin_shutdown", "game": GAME, "timestamp": "2026-01-01T00:01:00Z"},
            now=5.0,
        )

        events = self.read_events()
        self.assertEqual(
            ["SESSION_STARTED", "job.delivered", "SESSION_ENDED"],
            [event["event"] for event in events],
        )
        self.assertEqual("ets2", events[0]["game"])
        self.assertEqual(1, events[-1]["data"]["jobs_performed"])
        self.assertEqual(1.5, events[-1]["data"]["distance_driven_km"])
        self.assertEqual(4.0, events[-1]["data"]["driving_time_seconds"])

    def test_job_start_is_only_derived_from_a_known_empty_to_active_change(self) -> None:
        self.agent.process_message(
            {
                "type": "configuration",
                "game": GAME,
                "id": "job",
                "attributes": {"cargo": "Initial active cargo"},
            },
            now=0.0,
        )
        self.agent.process_message(
            {
                "type": "configuration",
                "game": GAME,
                "id": "job",
                "attributes": {},
            },
            now=1.0,
        )
        self.agent.process_message(
            {
                "type": "configuration",
                "game": GAME,
                "id": "job",
                "attributes": {"cargo": "Next cargo"},
            },
            now=2.0,
        )
        self.assertEqual(["JOB_STARTED"], [event["event"] for event in self.read_events()])

    def test_null_fields_remain_null_in_console_snapshot(self) -> None:
        self.agent.process_message(telemetry(10.0), now=1.0)
        snapshot = json.loads(self.output[-1])
        self.assertIsNone(snapshot["truck"]["speed_kmh"])
        self.assertIsNone(snapshot["position"]["x"])

    def test_session_ends_after_heartbeat_loss(self) -> None:
        self.agent.process_message(
            {"type": "plugin_init", "game": GAME, "timestamp": "2026-01-01T00:00:00Z"},
            now=0.0,
        )
        self.agent.check_timeout(now=5.0)
        self.assertEqual("telemetry_timeout", self.read_events()[-1]["data"]["reason"])

    def test_paused_game_plugin_heartbeat_keeps_session_and_records_heartbeat(self) -> None:
        self.agent.process_message(
            {"type": "plugin_init", "game": GAME, "timestamp": "2026-01-01T00:00:00Z"},
            now=0.0,
        )
        self.agent.process_message(
            {
                "type": "plugin_heartbeat",
                "game": GAME,
                "timestamp": "2026-01-01T00:00:15Z",
            },
            now=15.0,
        )
        self.agent.check_timeout(now=19.0)
        self.assertEqual(
            ["SESSION_STARTED", "SESSION_HEARTBEAT"],
            [event["event"] for event in self.read_events()],
        )

    def test_late_agent_detects_game_from_plugin_heartbeat(self) -> None:
        self.agent.process_message(
            {
                "type": "plugin_heartbeat",
                "game": GAME,
                "timestamp": "2026-01-01T00:00:01Z",
            },
            now=1.0,
        )
        events = self.read_events()
        self.assertEqual(["SESSION_STARTED"], [event["event"] for event in events])
        self.assertEqual("ets2", events[0]["game"])


if __name__ == "__main__":
    unittest.main()
