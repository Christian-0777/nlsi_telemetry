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
        self.agent.process_message(telemetry(0.0, state="driving"), now=0.0)
        self.agent.check_timeout(now=5.0)
        self.assertEqual("telemetry_timeout", self.read_events()[-1]["data"]["reason"])

    def test_paused_game_plugin_heartbeat_keeps_session_and_records_heartbeat(self) -> None:
        self.agent.process_message(telemetry(0.0, state="paused"), now=0.0)
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
        self.assertIsNone(self.agent.session_id)
        self.assertEqual([], self.read_events())

    def test_session_ids_skip_ids_already_persisted_in_event_history(self) -> None:
        events_path = Path(self.temp_dir.name) / "events.jsonl"
        events_path.write_text(
            json.dumps(
                {
                    "timestamp": "2026-10-08T00:00:00Z",
                    "event": "SESSION_STARTED",
                    "game": "ets2",
                    "data": {"session_id": "NLSI-20261008-0001"},
                }
            )
            + "\n",
            encoding="utf-8",
        )

        new_id = self.agent._new_session_id("2026-10-08T00:00:00Z")
        self.assertNotEqual("NLSI-20261008-0001", new_id)

    def test_effective_throttle_and_retarder_are_normalized(self) -> None:
        self.agent.process_message(
            {
                "type": "telemetry",
                "timestamp": "2026-01-01T00:00:00.000Z",
                "game": GAME,
                "state": "driving",
                "truck": {
                    "input_throttle": 0.85,
                    "effective_throttle": 0.37,
                    "input_brake": 0.60,
                    "effective_brake": 0.14,
                    "retarder_level": 2,
                    "cruise_control": 12.5,
                },
                "position": {"x": None, "y": None, "z": None},
                "configurations": {},
            },
            now=0.0,
        )
        snapshot = json.loads(self.output[-1])
        truck = snapshot["truck"]
        self.assertEqual(0.37, truck["throttle"])
        self.assertEqual(0.14, truck["brake"])
        self.assertEqual(2, truck["retarder_level"])
        self.assertTrue(truck["retarder_active"])
        self.assertEqual(12.5, truck["cruiseControlSpeed"])
        self.assertTrue(truck["cruiseControlActive"])

    def test_reconnected_session_gets_a_new_id_and_fresh_metrics(self) -> None:
        self.agent.process_message(telemetry(0.0, state="driving"), now=0.0)
        first_id = self.agent.session_id
        self.agent.process_message(
            {"type": "plugin_shutdown", "game": GAME, "timestamp": "2026-10-08T00:01:00Z"},
            now=60.0,
        )
        self.agent.process_message(
            {"type": "plugin_init", "game": GAME, "timestamp": "2026-10-08T00:02:00Z"},
            now=120.0,
        )
        self.agent.process_message(telemetry(5.0, state="driving"), now=120.0)

        self.assertNotEqual(first_id, self.agent.session_id)
        self.assertEqual(0.0, self.agent._session_metrics(120.0)["duration_seconds"])

    def test_provider_values_fallback_across_nlsi_and_rencloud(self) -> None:
        self.agent.process_message(
            {
                "type": "telemetry",
                "provider": "nlsi",
                "game": GAME,
                "state": "driving",
                "timestamp": "2026-01-01T00:00:00.000Z",
                "truck": {"speed_kmh": 32.0},
                "position": {"x": 1.0, "y": 2.0, "z": 3.0},
                "configurations": {},
            },
            now=0.0,
        )
        self.agent.process_message(
            {
                "type": "telemetry",
                "provider": "rencloud",
                "game": GAME,
                "state": "driving",
                "timestamp": "2026-01-01T00:00:01.000Z",
                "truck": {"remaining_distance_km": 42.5, "speed_kmh": 40.0},
                "position": {"x": 1.5, "y": 2.5, "z": 3.5},
                "configurations": {},
            },
            now=1.0,
        )

        merged = self.agent.combined_telemetry(now=1.0)
        self.assertEqual(32.0, merged["truck"]["speed_kmh"])
        self.assertEqual(42.5, merged["truck"]["remaining_distance_km"])

    def test_lifecycle_message_updates_timeout_window(self) -> None:
        self.agent.process_message(
            {
                "type": "lifecycle",
                "game": GAME,
                "state": "driving",
                "timestamp": "2026-01-01T00:00:00.000Z",
            },
            now=0.0,
        )
        self.assertIsNotNone(self.agent.last_packet_monotonic)
        self.agent.check_timeout(now=4.9)
        self.assertIsNotNone(self.agent.session_id)
        self.agent.check_timeout(now=5.1)
        self.assertIsNone(self.agent.session_id)


if __name__ == "__main__":
    unittest.main()
