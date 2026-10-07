import argparse
import ctypes
import json
import os
import select
import shutil
import socket
import sys
import time
from collections import deque
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable


ROOT = Path(__file__).resolve().parent
DEFAULT_OUTPUT_DIR = ROOT / "test" / "output"
DATA_DIR = ROOT / "data"
SESSION_DATA_DIR = DATA_DIR / "sessions"
LOGO_PATH = ROOT / "img" / "logo.png"
UDP_HOST = "127.0.0.1"
UDP_PORT = 28745
HEARTBEAT_INTERVAL_SECONDS = 15.0
TELEMETRY_TIMEOUT_SECONDS = 5.0


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


class TelemetryAgent:
    _session_counter = 0

    def __init__(
        self,
        output_dir: Path = DEFAULT_OUTPUT_DIR,
        output: Callable[[str], None] = print,
        monotonic: Callable[[], float] = time.monotonic,
    ) -> None:
        self.output_dir = output_dir
        self.events_path = output_dir / "events.jsonl"
        self.output = output
        self.monotonic = monotonic
        self.configurations: dict[str, dict[str, Any]] = {}
        self.game: dict[str, Any] | None = None
        self.session_id: str | None = None
        self._session_sequence = 0
        self.session_started_at: str | None = None
        self.session_started_monotonic: float | None = None
        self.last_accounted_monotonic: float | None = None
        self.last_packet_monotonic: float | None = None
        self.last_heartbeat_monotonic: float | None = None
        self.last_telemetry_timestamp: str | None = None
        self.latest_telemetry: dict[str, Any] | None = None
        self.packets_received = 0
        self.packet_times: deque[float] = deque()
        self.recent_events: deque[dict[str, Any]] = deque(maxlen=50)
        self.driving = False
        self.driving_time_seconds = 0.0
        self.distance_driven_km = 0.0
        self.last_odometer_km: float | None = None
        self.jobs_performed = 0
        self._seen_job_configurations: set[str] = set()
        self._job_pdf_path: Path | None = None
        self._job_pdf_exported = False

    def _set_game(self, game: Any) -> None:
        if not isinstance(game, dict):
            raise ValueError("Telemetry message is missing its game object.")
        game_id = game.get("id")
        if game_id not in {"ets2", "ats"}:
            raise ValueError(f"Unsupported or missing game id: {game_id!r}")
        self.game = game

    def _new_session_id(self, timestamp: str | None = None) -> str:
        if timestamp:
            try:
                dt = datetime.fromisoformat(timestamp.replace("Z", "+00:00")).astimezone(timezone.utc)
            except ValueError:
                dt = datetime.now(timezone.utc)
        else:
            dt = datetime.now(timezone.utc)
        self.__class__._session_counter += 1
        return f"NLSI-{dt.strftime('%Y%m%d')}-{self.__class__._session_counter:04d}"

    def _job_snapshot(self) -> dict[str, Any]:
        active_job = self.configurations.get("job") or self.configurations.get("car_job") or self.configurations.get("bus_job") or {}
        if not isinstance(active_job, dict):
            active_job = {}
        truck_config = self.configurations.get("truck", {}) if isinstance(self.configurations.get("truck", {}), dict) else {}
        truck = self.latest_telemetry.get("truck", {}) if isinstance(self.latest_telemetry, dict) and isinstance(self.latest_telemetry.get("truck", {}), dict) else {}
        position = self.latest_telemetry.get("position", {}) if isinstance(self.latest_telemetry, dict) and isinstance(self.latest_telemetry.get("position", {}), dict) else {}
        return {
            "session_id": self.session_id,
            "source": configured_value(active_job, "source", "source_city", "source_city_name"),
            "destination": configured_value(active_job, "destination", "destination_city", "destination_city_name"),
            "cargo": configured_value(active_job, "cargo", "cargo_name"),
            "cargo_mass_kg": configured_value(active_job, "cargo_mass_kg", "cargo_mass", "cargo_mass_kg"),
            "planned_distance_km": configured_value(active_job, "planned_distance_km", "planned_distance"),
            "remaining_distance_km": configured_value(active_job, "remaining_distance_km", "remaining_distance"),
            "delivery_time": configured_value(active_job, "delivery_time", "delivery_deadline"),
            "revenue": configured_value(active_job, "income", "job_income", "revenue"),
            "job_type": configured_value(active_job, "job_type", "type"),
            "truck_brand": configured_value(truck_config, "brand", "manufacturer"),
            "truck_model": configured_value(truck_config, "name", "model", "model_name"),
            "license_plate": configured_value(truck_config, "license_plate", "licenseplate", "plate"),
            "odometer_km": truck.get("odometer_km"),
            "fuel_liters": truck.get("fuel_liters"),
            "position": position,
        }

    def _pdf_storage_dir(self) -> Path:
        SESSION_DATA_DIR.mkdir(parents=True, exist_ok=True)
        return SESSION_DATA_DIR

    def _escape_pdf_text(self, value: str) -> str:
        return value.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")

    def _write_pdf_report(self, job_data: dict[str, Any]) -> Path:
        session_id = job_data.get("session_id") or self.session_id or "NLSI-SESSION"
        now_utc = datetime.now(timezone.utc)
        safe_name = session_id
        candidate = self._pdf_storage_dir() / f"{safe_name}.pdf"
        if candidate.exists():
            counter = 1
            while True:
                alt = self._pdf_storage_dir() / f"{safe_name}-{counter}.pdf"
                if not alt.exists():
                    candidate = alt
                    break
                counter += 1

        session_start = self.session_started_at or job_data.get("session_start") or now_utc.isoformat(timespec="milliseconds").replace("+00:00", "Z")
        end_time = job_data.get("end_time") or now_utc.isoformat(timespec="milliseconds").replace("+00:00", "Z")
        elapsed_seconds = max(0.0, self.driving_time_seconds)
        source = job_data.get("source") or "N/A"
        destination = job_data.get("destination") or "N/A"
        distance = job_data.get("distance_km") or job_data.get("planned_distance_km") or "N/A"
        if isinstance(distance, (int, float)) and not isinstance(distance, bool):
            distance_label = f"{float(distance):,.1f} km"
        else:
            distance_label = str(distance)

        logo_reference = str(LOGO_PATH.relative_to(ROOT)) if LOGO_PATH.exists() else "img/logo.png"
        lines = [
            "NLSI TELEMETRY",
            "COMPLETED DELIVERY REPORT",
            "",
            f"Session ID: {session_id}",
            f"Date: {now_utc.strftime('%B %d, %Y')}",
            "",
            "DELIVERY SUMMARY",
            f"FROM: {source}",
            f"TO: {destination}",
            f"DISTANCE: {distance_label}",
            "STATUS: DELIVERED",
            "",
            "SESSION",
            f"Session ID: {session_id}",
            f"Start Time: {session_start}",
            f"End Time: {end_time}",
            f"Elapsed Time: {display_duration(elapsed_seconds)}",
            f"Driving Time: {display_duration(elapsed_seconds)}",
            "",
            "TRUCK",
            f"Truck: {job_data.get('truck_brand') or job_data.get('truck_model') or 'N/A'}",
            f"Model: {job_data.get('truck_model') or 'N/A'}",
            f"Plate: {job_data.get('license_plate') or 'N/A'}",
            f"Odometer: {display_number(job_data.get('odometer_km'), 1)} km",
            f"Fuel: {display_number(job_data.get('fuel_liters'), 1)} L",
            "",
            "NAVIGATION",
            f"Planned Distance: {job_data.get('planned_distance_km') or 'N/A'}",
            f"Remaining Distance: {job_data.get('remaining_distance_km') or 'N/A'}",
            f"Revenue: {job_data.get('revenue') or 'N/A'}",
            "",
            "NLSI Telemetry Agent",
            "Generated automatically",
            f"Version {__package__ or '0.3.0'}",
            f"Logo: {logo_reference}",
        ]

        content_lines = []
        y = 770
        for line in lines:
            content_lines.append(f"BT /F1 12 Tf 72 {y} Td ({self._escape_pdf_text(line)}) Tj ET")
            y -= 18
        stream = "\n".join(content_lines)

        pdf_bytes = bytearray()
        pdf_bytes.extend(b"%PDF-1.4\n")
        objects = [
            b"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
            b"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n",
            b"3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>\nendobj\n",
            (
                f"4 0 obj\n<< /Length {len(stream.encode('latin-1', errors='replace'))} >>\nstream\n{stream}\nendstream\nendobj\n"
            ).encode("latin-1", errors="replace"),
            b"5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n",
        ]
        offsets = [0]
        for obj in objects:
            offsets.append(len(pdf_bytes))
            pdf_bytes.extend(obj)
        xref_start = len(pdf_bytes)
        pdf_bytes.extend(f"xref\n0 {len(objects) + 1}\n".encode("latin-1"))
        pdf_bytes.extend(b"0000000000 65535 f \n")
        for offset in offsets[1:]:
            pdf_bytes.extend(f"{offset:010d} 00000 n \n".encode("latin-1"))
        pdf_bytes.extend(
            f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\nstartxref\n{xref_start}\n%%EOF\n".encode("latin-1")
        )
        candidate.write_bytes(pdf_bytes)
        self._job_pdf_path = candidate
        return candidate

    def _start_pdf_export(self, payload: dict[str, Any]) -> Path | None:
        try:
            pdf_path = self._write_pdf_report({"session_id": self.session_id, **payload})
            self._job_pdf_exported = True
            return pdf_path
        except Exception:  # pragma: no cover - defensive path
            return None

    def _maybe_export_completed_job(self, event: str, data: dict[str, Any], timestamp: str | None = None) -> None:
        if event not in {"job.delivered", "car_job.delivered"}:
            return
        if self._job_pdf_exported:
            return
        active_job = self.configurations.get("job") or self.configurations.get("car_job") or self.configurations.get("bus_job") or {}
        summary = self._job_snapshot()
        summary.update(
            {
                "event": event,
                "distance_km": data.get("distance_km")
                or data.get("distance.km")
                or data.get("distance")
                or configured_value(active_job, "distance_km", "planned_distance_km", "planned_distance"),
                "session_start": self.session_started_at,
                "end_time": timestamp or utc_now(),
                "truck_brand": configured_value(self.configurations.get("truck", {}), "brand", "manufacturer"),
                "truck_model": configured_value(self.configurations.get("truck", {}), "name", "model", "model_name"),
                "license_plate": configured_value(self.configurations.get("truck", {}), "license_plate", "licenseplate", "plate"),
                "fuel_liters": (self.latest_telemetry or {}).get("truck", {}).get("fuel_liters") if isinstance(self.latest_telemetry, dict) else None,
                "odometer_km": (self.latest_telemetry or {}).get("truck", {}).get("odometer_km") if isinstance(self.latest_telemetry, dict) else None,
            }
        )
        self._start_pdf_export(summary)

    def _record_event(self, event: str, data: dict[str, Any], timestamp: str | None = None) -> None:
        if self.game is None:
            raise ValueError(f"Cannot record {event} without game metadata.")
        record = {
            "timestamp": timestamp or utc_now(),
            "event": event,
            "game": self.game["id"],
            "data": data,
        }
        self.output_dir.mkdir(parents=True, exist_ok=True)
        with self.events_path.open("a", encoding="utf-8", newline="\n") as events_file:
            events_file.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")
        self.recent_events.append(record)
        self.output(json.dumps(record, ensure_ascii=False, separators=(",", ":")))

    def _update_driving_clock(self, now: float) -> None:
        if self.last_accounted_monotonic is not None and self.driving:
            self.driving_time_seconds += max(0.0, now - self.last_accounted_monotonic)
        self.last_accounted_monotonic = now

    def _session_metrics(self, now: float) -> dict[str, Any]:
        elapsed = 0.0
        if self.session_started_monotonic is not None:
            elapsed = max(0.0, now - self.session_started_monotonic)
        driving_time = self.driving_time_seconds
        if self.driving and self.last_accounted_monotonic is not None:
            driving_time += max(0.0, now - self.last_accounted_monotonic)
        return {
            "duration_seconds": round(elapsed, 3),
            "driving_time_seconds": round(driving_time, 3),
            "distance_driven_km": round(self.distance_driven_km, 3),
            "jobs_performed": self.jobs_performed,
        }

    def _maybe_record_heartbeat(self, timestamp: str, now: float) -> None:
        if self.session_started_monotonic is None:
            return
        if self.last_heartbeat_monotonic is None:
            self.last_heartbeat_monotonic = now
            return
        if now - self.last_heartbeat_monotonic >= HEARTBEAT_INTERVAL_SECONDS:
            self._record_event("SESSION_HEARTBEAT", self._session_metrics(now), timestamp)
            self.last_heartbeat_monotonic = now

    def _start_session(self, timestamp: str, now: float) -> None:
        if self.game is None:
            raise ValueError("Cannot start a session without game metadata.")
        if self.session_id is None:
            self.session_id = self._new_session_id(timestamp)
        self.session_started_at = timestamp
        self.session_started_monotonic = now
        self.last_accounted_monotonic = now
        self.last_heartbeat_monotonic = now
        self._job_pdf_exported = False
        self._record_event(
            "SESSION_STARTED",
            {
                "session_id": self.session_id,
                "game_name": self.game.get("name"),
                "game_version": self.game.get("version"),
                "telemetry_api_version": self.game.get("telemetry_api_version"),
                "truck": self.configurations.get("truck", {}),
                "job": self.configurations.get("job")
                or self.configurations.get("car_job")
                or self.configurations.get("bus_job", {}),
            },
            timestamp,
        )

    def _end_session(self, reason: str, timestamp: str | None = None, now: float | None = None) -> None:
        if self.session_started_monotonic is None:
            return
        end_time = self.monotonic() if now is None else now
        self._update_driving_clock(end_time)
        self._record_event(
            "SESSION_ENDED",
            {"reason": reason, **self._session_metrics(end_time)},
            timestamp,
        )
        self.session_started_at = None
        self.session_started_monotonic = None
        self.last_accounted_monotonic = None
        self.last_heartbeat_monotonic = None
        self.driving = False
        self.last_odometer_km = None

    def _handle_configuration(self, message: dict[str, Any], now: float) -> None:
        config_id = message.get("id")
        attributes = message.get("attributes")
        if not isinstance(config_id, str) or not isinstance(attributes, dict):
            raise ValueError("Configuration message must contain a string id and object attributes.")
        previous = self.configurations.get(config_id)
        self.configurations[config_id] = attributes
        if config_id in {"job", "car_job", "bus_job"}:
            if config_id in self._seen_job_configurations and not previous and attributes:
                self._record_event(
                    "JOB_STARTED",
                    {"configuration": config_id, "job": attributes, "derived_from": "configuration_change"},
                    message.get("timestamp"),
                )
            self._seen_job_configurations.add(config_id)
        if self.session_started_monotonic is not None:
            self._update_driving_clock(now)

    def _handle_gameplay_event(self, message: dict[str, Any]) -> None:
        event = message.get("event")
        data = message.get("data")
        if not isinstance(event, str) or not isinstance(data, dict):
            raise ValueError("Gameplay message must contain a string event and object data.")
        if event in {"job.delivered", "car_job.delivered"}:
            self.jobs_performed += 1
        self._record_event(event, {"session_id": self.session_id, **data}, message.get("timestamp"))
        if event in {"job.delivered", "car_job.delivered"}:
            self._maybe_export_completed_job(event, data, message.get("timestamp"))

    def _snapshot(self, message: dict[str, Any], now: float) -> None:
        timestamp = message.get("timestamp")
        if not isinstance(timestamp, str):
            timestamp = utc_now()
        if self.session_started_monotonic is None:
            self._start_session(timestamp, now)
        self._update_driving_clock(now)

        configurations = message.get("configurations")
        if isinstance(configurations, dict):
            for config_id, attributes in configurations.items():
                if isinstance(config_id, str) and isinstance(attributes, dict):
                    self._handle_configuration(
                        {"id": config_id, "attributes": attributes, "timestamp": timestamp},
                        now,
                    )

        truck = message.get("truck")
        if isinstance(truck, dict):
            odometer = truck.get("odometer_km")
            if isinstance(odometer, (int, float)) and not isinstance(odometer, bool):
                if self.last_odometer_km is not None:
                    delta = odometer - self.last_odometer_km
                    if delta >= 0:
                        self.distance_driven_km += delta
                self.last_odometer_km = float(odometer)

        self.driving = message.get("state") == "driving"
        self.last_accounted_monotonic = now
        self.last_packet_monotonic = now
        snapshot = dict(message)
        snapshot["job"] = (
            self.configurations.get("job")
            or self.configurations.get("car_job")
            or self.configurations.get("bus_job", {})
        )
        snapshot["truck_configuration"] = self.configurations.get("truck", {})
        snapshot["trailer"] = {
            config_id: attributes
            for config_id, attributes in self.configurations.items()
            if config_id == "trailer" or config_id.startswith("trailer.")
        }
        self.latest_telemetry = snapshot
        self.last_telemetry_timestamp = timestamp
        self.output(json.dumps(snapshot, ensure_ascii=False, separators=(",", ":")))

        self._maybe_record_heartbeat(timestamp, now)

    def process_message(self, message: Any, now: float | None = None) -> None:
        if not isinstance(message, dict):
            raise ValueError("Telemetry packet must be a JSON object.")
        message_type = message.get("type")
        if not isinstance(message_type, str):
            raise ValueError("Telemetry packet is missing its type.")
        current_time = self.monotonic() if now is None else now
        if "game" in message:
            self._set_game(message["game"])
        timestamp = message.get("timestamp")

        if message_type == "configuration":
            self._handle_configuration(message, current_time)
        elif message_type == "gameplay_event":
            self._handle_gameplay_event(message)
        elif message_type == "plugin_init":
            self.last_packet_monotonic = current_time
            if self.session_started_monotonic is None:
                self._start_session(timestamp or utc_now(), current_time)
        elif message_type == "plugin_heartbeat":
            self.last_packet_monotonic = current_time
            if self.session_started_monotonic is None:
                self._start_session(timestamp or utc_now(), current_time)
            self._maybe_record_heartbeat(timestamp or utc_now(), current_time)
        elif message_type == "lifecycle":
            state = message.get("state")
            if state not in {"driving", "paused"}:
                raise ValueError(f"Unsupported lifecycle state: {state!r}")
            if self.session_started_monotonic is None:
                self._start_session(timestamp or utc_now(), current_time)
            if self.session_started_monotonic is not None:
                self._update_driving_clock(current_time)
                self.driving = state == "driving"
                self.last_accounted_monotonic = current_time
                self._record_event(
                    "DRIVING_RESUMED" if self.driving else "DRIVING_PAUSED",
                    {},
                    timestamp,
                )
        elif message_type == "telemetry":
            self._snapshot(message, current_time)
        elif message_type == "plugin_shutdown":
            self._end_session("game_plugin_shutdown", timestamp, current_time)
        else:
            raise ValueError(f"Unsupported telemetry packet type: {message_type!r}")
        self.packets_received += 1
        self.packet_times.append(current_time)
        while self.packet_times and current_time - self.packet_times[0] > 10.0:
            self.packet_times.popleft()

    def packets_per_second(self, now: float | None = None) -> float | None:
        current_time = self.monotonic() if now is None else now
        while self.packet_times and current_time - self.packet_times[0] > 10.0:
            self.packet_times.popleft()
        if len(self.packet_times) < 2:
            return None
        elapsed = min(10.0, max(1.0, current_time - self.packet_times[0]))
        return len(self.packet_times) / elapsed

    def check_timeout(self, now: float | None = None) -> None:
        current_time = self.monotonic() if now is None else now
        if (
            self.session_started_monotonic is not None
            and self.last_packet_monotonic is not None
            and current_time - self.last_packet_monotonic >= TELEMETRY_TIMEOUT_SECONDS
        ):
            self._end_session("telemetry_timeout", now=current_time)

    def close(self) -> None:
        self._end_session("agent_stopped")


def app_version() -> str:
    try:
        payload = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
    except (FileNotFoundError, OSError, json.JSONDecodeError):
        return "0.3.1"
    if isinstance(payload, dict):
        version = payload.get("version")
        if isinstance(version, str) and version.strip():
            return version
    return "0.3.1"


def display_value(value: Any) -> str:
    if value is None or isinstance(value, bool):
        return "--" if value is None else ("Yes" if value else "No")
    return str(value)


def coerce_number(value: Any) -> float | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str):
        try:
            return float(value)
        except ValueError:
            return None
    return None


def first_available(mapping: dict[str, Any], *keys: str) -> Any:
    for key in keys:
        if key in mapping and mapping[key] is not None:
            return mapping[key]
    return None


def is_auto_control_active(truck: dict[str, Any], *keys: str) -> bool:
    for key in keys:
        value = truck.get(key)
        if isinstance(value, bool):
            if value:
                return True
            continue
        if isinstance(value, str):
            lowered = value.strip().lower()
            if lowered in {"auto", "automatic", "cruise", "enabled", "on", "active"}:
                return True
    return False


def display_cruise_value(truck: dict[str, Any]) -> str:
    value = first_available(
        truck,
        "cruise_control",
        "cruise_control_value",
        "cruise_speed",
        "cruise_set_speed",
        "cruise_target_speed",
    )
    if value is None:
        if is_auto_control_active(truck, "cruise_control_active", "cruise_active", "cruise_enabled"):
            return "ACTIVE"
        return "N/A"
    numeric = coerce_number(value)
    if numeric is not None:
        return display_number(numeric, 1)
    if isinstance(value, bool):
        return "ON" if value else "OFF"
    if isinstance(value, str):
        lowered = value.strip().lower()
        if lowered in {"active", "enabled", "on", "true"}:
            return "ACTIVE"
        if lowered in {"off", "disabled", "false"}:
            return "OFF"
        return value.strip() or "N/A"
    return "N/A"


def display_adaptive_cruise_value(truck: dict[str, Any]) -> str:
    value = first_available(
        truck,
        "adaptive_cruise",
        "adaptive_cruise_control",
        "adaptive_cruise_value",
        "adaptive_cruise_active",
        "acc",
        "acc_active",
    )
    if value is None:
        return "N/A"
    numeric = coerce_number(value)
    if numeric is not None:
        return display_number(numeric, 1)
    if isinstance(value, bool):
        return "ON" if value else "OFF"
    if isinstance(value, str):
        lowered = value.strip().lower()
        if lowered in {"active", "enabled", "on", "true"}:
            return "ON"
        if lowered in {"off", "disabled", "false"}:
            return "OFF"
        return value.strip() or "N/A"
    return "N/A"


def display_retarder_value(truck: dict[str, Any]) -> str:
    if is_auto_control_active(truck, "retarder_automatic", "retarder_auto", "automatic_retarder", "retarder_cruise"):
        return "A"
    value = first_available(truck, "retarder", "retarder_step", "retarder_level", "retarder_value")
    if value is None:
        return "N/A"
    numeric = coerce_number(value)
    if numeric is not None:
        return str(int(round(numeric))) if numeric == int(round(numeric)) else str(numeric)
    if isinstance(value, str):
        lowered = value.strip().lower()
        if lowered in {"auto", "automatic", "a"}:
            return "A"
        return value.strip() or "N/A"
    return "N/A"


def display_throttle_value(truck: dict[str, Any]) -> str:
    if is_auto_control_active(
        truck,
        "cruise_control_active",
        "cruise_active",
        "auto_throttle",
        "throttle_auto",
        "throttle_control_source",
    ):
        return "A"
    value = truck.get("throttle")
    numeric = coerce_number(value)
    if numeric is None:
        return "N/A"
    return display_percent(numeric)


def display_brake_value(truck: dict[str, Any]) -> str:
    if is_auto_control_active(
        truck,
        "cruise_control_active",
        "cruise_active",
        "auto_brake",
        "brake_auto",
        "brake_control_source",
    ):
        return "A"
    value = truck.get("brake")
    numeric = coerce_number(value)
    if numeric is None:
        return "N/A"
    return display_percent(numeric)


def display_gear_value(truck: dict[str, Any]) -> str:
    value = truck.get("gear")
    if value is None:
        return "N/A"
    if isinstance(value, str):
        normalized = value.strip().upper()
        return normalized if normalized else "N/A"
    numeric = coerce_number(value)
    if numeric is None:
        return "N/A"
    if numeric == 0:
        return "N"
    if numeric < 0:
        return "R"
    if is_auto_control_active(truck, "automatic_transmission", "transmission_automatic", "gearbox_automatic"):
        return f"{int(round(numeric))}A"
    return str(int(round(numeric)))


def display_number(value: Any, places: int = 1, suffix: str = "") -> str:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return "--"
    return f"{value:,.{places}f}{suffix}"


def display_duration(value: Any) -> str:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return "--"
    seconds = max(0, int(value))
    hours, remainder = divmod(seconds, 3600)
    minutes, seconds = divmod(remainder, 60)
    return f"{hours:02d}:{minutes:02d}:{seconds:02d}"


def display_percent(value: Any) -> str:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return "--"
    return display_number(value * 100, 0, "%")


def display_time(value: str | None) -> str:
    if not value:
        return "--"
    try:
        timestamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
        return timestamp.astimezone().strftime("%H:%M:%S")
    except ValueError:
        return "--"


def configured_value(configuration: dict[str, Any], *keys: str) -> Any:
    values = {key.lower().replace("_", "").replace(".", ""): value for key, value in configuration.items()}
    for key in keys:
        normalized = key.lower().replace("_", "").replace(".", "")
        if normalized in values:
            return values[normalized]
    return None


class DashboardRenderer:
    def __init__(self, agent: TelemetryAgent) -> None:
        self.agent = agent

    def _connection_state(self, now: float) -> tuple[bool, str]:
        connected = (
            self.agent.session_started_monotonic is not None
            and self.agent.last_packet_monotonic is not None
            and now - self.agent.last_packet_monotonic < TELEMETRY_TIMEOUT_SECONDS
        )
        if not connected:
            return False, "DISCONNECTED"
        return True, "LIVE" if self.agent.driving else "PAUSED"

    def dashboard(self, now: float | None = None) -> list[str]:
        current_time = self.agent.monotonic() if now is None else now
        connected, state = self._connection_state(current_time)
        packet = self.agent.latest_telemetry or {}
        truck = packet.get("truck") if isinstance(packet.get("truck"), dict) else {}
        position = packet.get("position") if isinstance(packet.get("position"), dict) else {}
        configurations = packet.get("configurations")
        if not isinstance(configurations, dict):
            configurations = {}
        truck_config = packet.get("truck_configuration")
        if not isinstance(truck_config, dict):
            truck_config = configurations.get("truck", {})
        if not isinstance(truck_config, dict):
            truck_config = {}
        job = packet.get("job")
        if not isinstance(job, dict):
            job = {}
        is_active_job = bool(job)
        game = self.agent.game or {}

        speed_kmh = truck.get("speed_kmh")
        if speed_kmh is None:
            speed_mps = truck.get("speed_mps")
            if isinstance(speed_mps, (int, float)) and not isinstance(speed_mps, bool):
                speed_kmh = speed_mps * 3.6
        steering = truck.get("steering")
        adblue = truck.get("adblue_warning")
        adblue_status = "--" if adblue is None else ("WARNING" if adblue else "OK")
        fuel_capacity = configured_value(
            truck_config, "fuel_capacity_liters", "fuel_capacity_l", "fuel_capacity"
        )
        navigation_distance = truck.get("navigation_distance_m")
        navigation_distance_km = (
            navigation_distance / 1000
            if isinstance(navigation_distance, (int, float)) and not isinstance(navigation_distance, bool)
            else None
        )
        metrics = self.agent._session_metrics(current_time)
        pps = self.agent.packets_per_second(current_time)
        source = configured_value(job, "source", "source_city", "source_city_name")
        destination = configured_value(job, "destination", "destination_city", "destination_city_name")
        gear = truck.get("gear")
        gear_display = display_gear_value(truck)
        if gear is None:
            gear_display = "N/A"
        cruise_state = display_cruise_value(truck)
        adaptive_cruise_state = display_adaptive_cruise_value(truck)

        if (
            isinstance(navigation_distance, (int, float))
            and not isinstance(navigation_distance, bool)
            and isinstance(speed_kmh, (int, float))
            and not isinstance(speed_kmh, bool)
            and speed_kmh > 0
        ):
            eta_seconds = navigation_distance / max(speed_kmh / 3.6, 1e-9)
            eta_label = display_duration(eta_seconds)
        else:
            eta_label = "--:--:--"

        session_id = self.agent.session_id or "NLSI-SESSION"
        lines = [
            "NLSI TELEMETRY AGENT",
            "-" * 80,
            f"{state} | {display_value(game.get('name'))} {display_value(game.get('version'))}",
            f"NLSI Telemetry: {app_version()} | SCS Telemetry API: {display_value(game.get('telemetry_api_version'))}",
            "TRUCK",
            f"{display_value(configured_value(truck_config, 'brand'))} | "
            f"{display_value(configured_value(truck_config, 'name', 'model', 'model_name'))}",
            f"Plate: {display_value(configured_value(truck_config, 'license_plate', 'licenseplate', 'plate'))}  "
            f"Fuel: {display_number(truck.get('fuel_liters'))} / {display_number(fuel_capacity)} L",
            f"Odometer: {display_number(truck.get('odometer_km'))} km  AdBlue: {adblue_status}",
            "DRIVING",
            f"Speed: {display_number(speed_kmh)} km/h  RPM: {display_number(truck.get('rpm'), 0)}  "
            f"Gear: {gear_display}",
            f"Steering: {display_percent(steering)}  Throttle: {display_throttle_value(truck)}  "
            f"Brake: {display_brake_value(truck)}  "
            f"Retarder: {display_retarder_value(truck)}",
            "CRUISE CONTROL",
            f"Cruise Control: {cruise_state} | Adaptive Cruise: {adaptive_cruise_state}",
            "POSITION",
            f"X: {display_number(position.get('x'), 2)}  Y: {display_number(position.get('y'), 2)}  "
            f"Z: {display_number(position.get('z'), 2)}",
            f"Heading: {display_number(position.get('heading_degrees'), 1)} deg  "
            f"Pitch: {display_number(position.get('pitch_degrees'), 1)} deg  "
            f"Roll: {display_number(position.get('roll_degrees'), 1)} deg",
            "NAVIGATION",
            f"Distance: {display_number(navigation_distance_km)} km  "
            f"Time: {display_duration(truck.get('navigation_time_s'))}  ETA: {eta_label}",
            "JOB",
            (
                f"Active: {display_value(source)} -> {display_value(destination)} | "
                f"Cargo: {display_value(configured_value(job, 'cargo', 'cargo_name'))}"
                if is_active_job
                else "No active job"
            ),
            (
                f"Planned: {display_number(configured_value(job, 'planned_distance_km', 'planned_distance'))} km  "
                f"Income: {display_number(configured_value(job, 'income', 'job_income'), 0)}  "
                f"Delivery: {display_value(configured_value(job, 'delivery_time', 'delivery_time_s'))}"
                if is_active_job
                else ""
            ),
            "SESSION",
            f"ID: {session_id}  Started: {display_time(self.agent.session_started_at)}  "
            f"Elapsed: {display_duration(metrics['duration_seconds'])}  "
            f"Status: {state}",
            f"Distance: {display_number(metrics['distance_driven_km'])} km  "
            f"Jobs completed: {metrics['jobs_performed']}",
            "CONNECTION",
            f"Plugin: {'CONNECTED' if connected else 'DISCONNECTED'}  "
            f"Last telemetry: {display_time(self.agent.last_telemetry_timestamp)}",
            f"Packets: {self.agent.packets_received}  "
            f"Packets/sec: {display_number(pps, 1) if pps is not None else '--'}",
            "-" * 80,
            f"{state} | Last update: {datetime.now().astimezone().strftime('%H:%M:%S')}",
            "Press Q to quit | D debug | E events",
        ]
        if not is_active_job:
            job_index = lines.index("JOB")
            connection_index = lines.index("CONNECTION")
            del lines[job_index + 2 : connection_index]
        return lines

    def debug(self) -> list[str]:
        lines = ["NLSI TELEMETRY AGENT - RAW TELEMETRY (latest packet)", "-" * 80]
        if self.agent.latest_telemetry is None:
            lines.append("Waiting for telemetry packet...")
        else:
            lines.extend(json.dumps(self.agent.latest_telemetry, ensure_ascii=False, indent=2).splitlines())
        lines.extend(["-" * 80, "Press D for dashboard | E events | Q to quit"])
        return lines

    def events(self) -> list[str]:
        lines = ["NLSI TELEMETRY AGENT - RECENT EVENTS", "-" * 80, "TIME       EVENT"]
        lines.extend(
            f"{display_time(event.get('timestamp')):<10} {event.get('event', '--')}"
            for event in list(self.agent.recent_events)[-20:]
        )
        if not self.agent.recent_events:
            lines.append("No events recorded in this run.")
        lines.extend(["-" * 80, "Press E for dashboard | D debug | Q to quit"])
        return lines


class ConsoleInput:
    def __init__(self) -> None:
        self._msvcrt: Any = None
        if os.name == "nt":
            import msvcrt

            self._msvcrt = msvcrt

    def read_key(self) -> str | None:
        if self._msvcrt is not None:
            if not self._msvcrt.kbhit():
                return None
            key = self._msvcrt.getwch()
            if key in {"\x00", "\xe0"}:
                self._msvcrt.getwch()
                return None
            return key.lower()
        readable, _, _ = select.select([sys.stdin], [], [], 0)
        return sys.stdin.read(1).lower() if readable else None


class TerminalDisplay:
    def __init__(self) -> None:
        self.stream = sys.stdout
        self.is_tty = self.stream.isatty()
        self.use_ansi = self.is_tty and os.name != "nt"
        if self.is_tty and os.name == "nt":
            self.use_ansi = self._enable_windows_vt()
        self._printed_non_tty = False

    @staticmethod
    def _enable_windows_vt() -> bool:
        kernel32 = ctypes.windll.kernel32
        kernel32.GetStdHandle.argtypes = [ctypes.c_ulong]
        kernel32.GetStdHandle.restype = ctypes.c_void_p
        kernel32.GetConsoleMode.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
        kernel32.SetConsoleMode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        handle = kernel32.GetStdHandle(-11)
        mode = ctypes.c_ulong()
        if not kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
            return False
        return bool(kernel32.SetConsoleMode(handle, mode.value | 0x0004))

    def draw(self, lines: list[str]) -> None:
        if not self.is_tty and self._printed_non_tty:
            return
        width, height = shutil.get_terminal_size((100, 30))
        visible = [line[:width] for line in lines[:height]]
        if self.is_tty:
            if self.use_ansi:
                self.stream.write("\x1b[2J\x1b[H")
            elif os.name == "nt":
                os.system("cls")
        self.stream.write("\n".join(visible) + "\n")
        self.stream.flush()
        if not self.is_tty:
            self._printed_non_tty = True


def handle_console_key(view: str, key: str | None) -> tuple[str, bool]:
    if key == "q":
        return view, True
    if key == "d":
        return ("dashboard" if view == "debug" else "debug"), False
    if key == "e":
        return ("dashboard" if view == "events" else "events"), False
    return view, False


def run_agent() -> None:
    parser = argparse.ArgumentParser(description="NLSI local ETS2/ATS telemetry agent")
    parser.add_argument("--port", type=int, default=UDP_PORT, help="UDP port used by the local SCS plugin")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")

    agent = TelemetryAgent(output=lambda _message: None)
    renderer = DashboardRenderer(agent)
    keyboard = ConsoleInput()
    display = TerminalDisplay()
    view = "dashboard"
    refresh_interval = 0.25
    next_refresh = 0.0
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
        listener.bind((UDP_HOST, args.port))
        listener.settimeout(0.1)
        display.draw(renderer.dashboard())
        try:
            while True:
                try:
                    packet, _ = listener.recvfrom(65535)
                except socket.timeout:
                    agent.check_timeout()
                else:
                    try:
                        message = json.loads(packet.decode("utf-8"))
                        agent.process_message(message)
                    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
                        print(f"Rejected telemetry packet: {error}", file=sys.stderr, flush=True)

                key = keyboard.read_key()
                view, should_quit = handle_console_key(view, key)
                if should_quit:
                    break
                if key in {"d", "e"}:
                    next_refresh = 0.0

                now = time.monotonic()
                if now >= next_refresh:
                    lines = (
                        renderer.debug()
                        if view == "debug"
                        else renderer.events()
                        if view == "events"
                        else renderer.dashboard(now)
                    )
                    display.draw(lines)
                    next_refresh = now + refresh_interval
        except KeyboardInterrupt:
            pass
        finally:
            agent.close()


if __name__ == "__main__":
    run_agent()
