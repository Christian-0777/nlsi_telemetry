import json
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from agent import DashboardRenderer, TelemetryAgent, display_trailer_value, handle_console_key
from gui_app import format_utc_and_manila_time


GAME = {
    "id": "ets2",
    "name": "Euro Truck Simulator 2",
    "version": "1.61.1.1s",
    "telemetry_api_version": "1.01",
}


def telemetry(odometer: float = 9645.4) -> dict:
    return {
        "type": "telemetry",
        "timestamp": "2026-10-07T08:36:46.000Z",
        "game": GAME,
        "state": "driving",
        "truck": {
            "speed_mps": 10.0,
            "speed_kmh": 36.0,
            "rpm": 1500,
            "gear": 0,
            "steering": -0.5,
            "throttle": 0.25,
            "brake": 0.05,
            "fuel_liters": 623.7,
            "odometer_km": odometer,
            "navigation_distance_m": 2300,
            "navigation_time_s": 3661,
            "adblue_warning": False,
        },
        "position": {
            "x": 41425.29,
            "y": 70.2,
            "z": 17978.88,
            "heading_degrees": 156.0,
            "pitch_degrees": 0.0,
            "roll_degrees": 0.0,
        },
        "configurations": {
            "truck": {
                "brand": "Scania",
                "name": "S",
                "license_plate": "XTIANFOUR",
                "fuel_capacity": 1000,
            },
            "job": {
                "source": "Berlin",
                "destination": "Paris",
                "cargo": "Furniture",
                "planned_distance_km": 1050,
                "income": 25000,
                "delivery_time": "2026-10-08T12:00:00Z",
            },
        },
    }


class DashboardRendererTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.agent = TelemetryAgent(Path(self.temp_dir.name), output=lambda _message: None)
        self.renderer = DashboardRenderer(self.agent)

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def test_dashboard_formats_live_telemetry_and_configuration(self) -> None:
        self.agent.process_message(telemetry(), now=1.0)
        lines = self.renderer.dashboard(now=1.5)
        dashboard = "\n".join(lines)

        self.assertLessEqual(len(lines), 30)
        self.assertIn("CONNECTION", dashboard)
        self.assertIn("Press Q to quit | D debug | E events", dashboard)
        self.assertIn("LIVE | Euro Truck Simulator 2 1.61.1.1s", dashboard)
        self.assertIn("SCS Telemetry API: 1.01", dashboard)
        self.assertIn("Scania | S", dashboard)
        self.assertIn("Plate: XTIANFOUR", dashboard)
        self.assertIn("Fuel: 623.7 / 1,000.0 L", dashboard)
        self.assertIn("Odometer: 9,645.4 km  AdBlue: OK", dashboard)
        self.assertIn("Speed: 36.0 km/h  RPM: 1,500  Gear: N", dashboard)
        self.assertIn("Steering: -50%  Throttle: 25%  Brake: 5%", dashboard)
        self.assertIn("X: 41,425.29  Y: 70.20  Z: 17,978.88", dashboard)
        self.assertIn("Distance: 2.3 km  Time: 01:01:01", dashboard)
        self.assertIn("Berlin -> Paris", dashboard)
        self.assertIn("Cargo: Furniture", dashboard)

    def test_dashboard_reports_distinct_app_and_sdk_versions(self) -> None:
        self.agent.process_message(telemetry(), now=1.0)
        dashboard = "\n".join(self.renderer.dashboard(now=1.5))

        self.assertIn("NLSI Telemetry: 1.4.8", dashboard)
        self.assertIn("SCS Telemetry API: 1.01", dashboard)

    def test_format_utc_and_manila_time_contains_date_on_both_sides(self) -> None:
        formatted = format_utc_and_manila_time(datetime(2026, 10, 8, 3, 15, 2, tzinfo=timezone.utc))
        self.assertEqual("10/08/26 - 03:15:02 - UTC | 10/08/26 - 11:15:02 - Asia/Manila", formatted)

    def test_dashboard_uses_real_cruise_retarder_throttle_brake_and_gear_values(self) -> None:
        packet = telemetry()
        packet["truck"].update(
            {
                "cruise_control": 67.5,
                "adaptive_cruise": 2,
                "retarder": 2,
                "throttle": 0.72,
                "brake": 0.18,
                "gear": 6,
                "automatic_transmission": True,
            }
        )
        self.agent.process_message(packet, now=1.0)
        dashboard = "\n".join(self.renderer.dashboard(now=1.5))

        self.assertIn("Cruise Control: 67.5", dashboard)
        self.assertIn("Adaptive Cruise: 2.0", dashboard)
        self.assertIn("Retarder: 2", dashboard)
        self.assertIn("Throttle: 72%", dashboard)
        self.assertIn("Brake: 18%", dashboard)
        self.assertIn("Gear: 6A", dashboard)

    def test_trailer_display_filters_raw_config_ids_and_keeps_connected_names(self) -> None:
        trailers = {
            "trailer": {"id": "trailer.0", "name": "trailer.0"},
            "trailer.0": {"id": "trailer.0", "name": "Box Trailer", "connected": True},
            "trailer.1": {"id": "trailer.1", "name": "Reefer", "connected": False},
            "trailer.2": {"id": "trailer.2", "name": "Tank Trailer", "connected": True},
        }

        self.assertEqual("Box Trailer, Tank Trailer", display_trailer_value(trailers))

    def test_dashboard_marks_missing_values_and_disconnected_state(self) -> None:
        dashboard = "\n".join(self.renderer.dashboard(now=10.0))

        self.assertIn("DISCONNECTED", dashboard)
        self.assertIn("No active job", dashboard)
        self.assertIn("Fuel: -- / -- L", dashboard)
        self.assertIn("Heading: -- deg", dashboard)

    def test_debug_view_shows_only_latest_pretty_packet(self) -> None:
        self.agent.process_message(telemetry(100.0), now=1.0)
        self.agent.process_message(telemetry(200.0), now=2.0)
        debug = "\n".join(self.renderer.debug())

        self.assertIn('"odometer_km": 200.0', debug)
        self.assertNotIn('"odometer_km": 100.0', debug)
        self.assertIn("\n  \"truck\": {", debug)
        self.assertEqual(2, self.agent.packets_received)

    def test_event_view_renders_recent_recorded_events(self) -> None:
        self.agent.process_message(
            {
                "type": "lifecycle",
                "game": GAME,
                "state": "paused",
                "timestamp": "2026-10-07T08:34:21Z",
            },
            now=2.0,
        )
        events = "\n".join(self.renderer.events())

        self.assertIn("TIME       EVENT", events)
        self.assertIn("SESSION_STARTED", events)
        self.assertIn("DRIVING_PAUSED", events)
        self.assertEqual(1, self.agent.packets_received)

    def test_event_view_keeps_the_latest_events(self) -> None:
        for index in range(25):
            self.agent.process_message(
                {
                    "type": "gameplay_event",
                    "game": GAME,
                    "event": f"EVENT_{index}",
                    "data": {},
                },
                now=float(index),
            )

        events = "\n".join(self.renderer.events())

        self.assertNotIn("EVENT_0", events)
        self.assertIn("EVENT_24", events)

    def test_console_keys_toggle_views_and_request_quit(self) -> None:
        self.assertEqual(("debug", False), handle_console_key("dashboard", "d"))
        self.assertEqual(("dashboard", False), handle_console_key("debug", "d"))
        self.assertEqual(("events", False), handle_console_key("dashboard", "e"))
        self.assertEqual(("dashboard", False), handle_console_key("events", "e"))
        self.assertEqual(("events", True), handle_console_key("events", "q"))

    def test_clean_close_records_session_end_to_jsonl(self) -> None:
        self.agent.process_message(telemetry(), now=1.0)
        self.agent.close()

        events_path = Path(self.temp_dir.name) / "events.jsonl"
        records = [json.loads(line) for line in events_path.read_text(encoding="utf-8").splitlines()]
        self.assertEqual(["SESSION_STARTED", "SESSION_ENDED"], [record["event"] for record in records])
        self.assertEqual("agent_stopped", records[-1]["data"]["reason"])


if __name__ == "__main__":
    unittest.main()
