from __future__ import annotations

import argparse
import copy
import json
import queue
import socket
import threading
import time
import tkinter as tk
import webbrowser
from datetime import datetime, timedelta, timezone
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from typing import Any

from agent import (
    DEFAULT_OUTPUT_DIR,
    ROOT,
    TELEMETRY_TIMEOUT_SECONDS,
    TelemetryAgent,
    app_channel,
    app_product,
    app_release_label,
    app_version,
    configured_value,
    display_adaptive_cruise_value,
    display_brake_value,
    display_cruise_value,
    display_duration,
    display_gear_value,
    display_number,
    display_retarder_value,
    display_throttle_value,
    display_trailer_value,
)


UTC_PLUS_8 = timezone(timedelta(hours=8))
MANILA_TZ = timezone(timedelta(hours=8))
SOCIAL_KEYS = {
    "SOCIAL_TIKTOK_URL": "TikTok",
    "SOCIAL_DISCORD_URL": "Discord",
    "SOCIAL_FACEBOOK_URL": "Facebook",
    "SOCIAL_YOUTUBE_URL": "YouTube",
    "NLSI_WEBSITE_URL": "Website",
}
TAB_NAMES = ("Main", "Finished Jobs", "PDF Export", "Debug", "Events", "Active Mods", "About Us")
PROJECT_URL = "https://github.com/Christian-0777/nlsi_telemetry"


def read_event_records(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as event_file:
        for line_number, line in enumerate(event_file, start=1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"Invalid event JSON at line {line_number}: {error.msg}") from error
            if not isinstance(record, dict):
                raise ValueError(f"Event record at line {line_number} must be a JSON object.")
            records.append(record)
    return records


def _first_value(mapping: dict[str, Any], *keys: str) -> Any:
    return configured_value(mapping, *keys)


def _game_name(record: dict[str, Any]) -> str:
    game = record.get("game")
    if game == "ets2":
        return "ETS2"
    if game == "ats":
        return "ATS"
    return str(game) if game else "--"


def jobs_from_events(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    jobs: list[dict[str, Any]] = []
    for record_index, record in enumerate(records):
        event = str(record.get("event") or "").lower()
        data = record.get("data")
        if not isinstance(data, dict):
            continue
        if event == "job_started":
            job_data = data.get("job")
            if not isinstance(job_data, dict):
                job_data = {}
            jobs.append(
                {
                    "job_id": _first_value(job_data, "job_id", "id"),
                    "session_id": data.get("session_id"),
                    "game": _game_name(record),
                    "status": "Pending",
                    "start_time": record.get("timestamp"),
                    "end_time": None,
                    "details": dict(job_data),
                    "record_index": record_index,
                }
            )
            continue
        if event not in {"job.delivered", "car_job.delivered", "job.cancelled", "car_job.cancelled"}:
            continue

        status = "Delivered" if event.endswith(".delivered") else "Cancelled"
        session_id = data.get("session_id")
        event_job_id = _first_value(data, "job_id", "id")
        target = None
        if event_job_id is not None:
            target = next(
                (
                    job
                    for job in reversed(jobs)
                    if job["status"] == "Pending"
                    and job.get("session_id") == session_id
                    and job.get("job_id") == event_job_id
                ),
                None,
            )
        if target is None:
            target = next(
                (
                    job
                    for job in reversed(jobs)
                    if job["status"] == "Pending" and job.get("session_id") == session_id
                ),
                None,
            )

        snapshot = data.get("job_snapshot")
        if not isinstance(snapshot, dict):
            snapshot = {}
        if target is None:
            target = {
                "job_id": event_job_id,
                "session_id": session_id,
                "game": _game_name(record),
                "status": status,
                "start_time": None,
                "end_time": record.get("timestamp"),
                "details": {},
                "record_index": record_index,
            }
            jobs.append(target)
        target["status"] = status
        target["end_time"] = record.get("timestamp")
        target["game"] = _game_name(record)
        target["details"].update(snapshot)
        target["details"].update(
            {
                key: value
                for key, value in data.items()
                if key not in {"session_id", "job_snapshot"}
            }
        )
        if target.get("job_id") is None:
            target["job_id"] = event_job_id or _first_value(snapshot, "job_id", "id")
    return jobs


def filter_jobs(jobs: list[dict[str, Any]], status_filter: str) -> list[dict[str, Any]]:
    if status_filter == "All":
        return list(jobs)
    return [job for job in jobs if job.get("status") == status_filter]


def parse_env_urls(paths: tuple[Path, ...]) -> dict[str, str]:
    values: dict[str, str] = {}
    for path in paths:
        if not path.is_file():
            continue
        with path.open("r", encoding="utf-8") as env_file:
            for line in env_file:
                stripped = line.strip()
                if not stripped or stripped.startswith("#") or "=" not in stripped:
                    continue
                key, value = stripped.split("=", 1)
                key = key.strip()
                if key not in SOCIAL_KEYS:
                    continue
                value = value.strip().strip("\"'")
                if value.startswith(("https://", "http://")):
                    values[key] = value
    return values


def redact_sensitive(value: Any) -> Any:
    if isinstance(value, dict):
        redacted: dict[str, Any] = {}
        for key, item in value.items():
            normalized = "".join(character for character in str(key).lower() if character.isalnum())
            if any(secret_term in normalized for secret_term in ("apikey", "token", "password", "secret", "credential", "authorization")):
                redacted[str(key)] = "[REDACTED]"
            else:
                redacted[str(key)] = redact_sensitive(item)
        return redacted
    if isinstance(value, list):
        return [redact_sensitive(item) for item in value]
    return value


def format_real_time(value: str | None) -> str:
    if not value:
        return "--"
    try:
        timestamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return "--"
    return format_utc_and_manila_time(timestamp)


def format_utc_and_manila_time(now: datetime | None = None) -> str:
    current = datetime.now(timezone.utc) if now is None else now
    if current.tzinfo is None:
        current = current.replace(tzinfo=timezone.utc)
    utc_current = current.astimezone(timezone.utc)
    manila_now = utc_current.astimezone(MANILA_TZ)
    return f"{utc_current.strftime('%m/%d/%y - %H:%M:%S')} - UTC | {manila_now.strftime('%m/%d/%y - %H:%M:%S')} - Asia/Manila"


def format_clock(value: str | None) -> str:
    if not value:
        return "--"
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone(UTC_PLUS_8).strftime("%H:%M:%S")
    except ValueError:
        return "--"


def _game_title(game: dict[str, Any] | None) -> str:
    if not isinstance(game, dict):
        return "--"
    return str(game.get("name") or game.get("id") or "--")


def make_snapshot(agent: TelemetryAgent, last_error: str | None, worker_status: str) -> dict[str, Any]:
    now = agent.monotonic()
    packet = copy.deepcopy(agent.latest_telemetry) if agent.latest_telemetry else {}
    truck = packet.get("truck") if isinstance(packet.get("truck"), dict) else {}
    job = packet.get("job") if isinstance(packet.get("job"), dict) else {}
    truck_config = packet.get("truck_configuration")
    if not isinstance(truck_config, dict):
        truck_config = {}
    game = copy.deepcopy(agent.game) if agent.game else {}
    session_active = agent.session_started_monotonic is not None
    connected = (
        session_active
        and agent.last_packet_monotonic is not None
        and now - agent.last_packet_monotonic < TELEMETRY_TIMEOUT_SECONDS
    )
    if not session_active:
        state = "Disconnected" if agent.last_session_id else "Idle"
    elif not connected:
        state = "Disconnected"
    elif agent.driving:
        state = "Driving"
    else:
        state = "Paused"

    metrics = agent._session_metrics(now) if session_active else agent.last_session_metrics
    speed_kmh = truck.get("speed_kmh")
    if speed_kmh is None:
        speed_mps = truck.get("speed_mps")
        if isinstance(speed_mps, (int, float)) and not isinstance(speed_mps, bool):
            speed_kmh = speed_mps * 3.6
    nav_distance = truck.get("navigation_distance_m")
    eta = "--"
    if (
        isinstance(nav_distance, (int, float))
        and not isinstance(nav_distance, bool)
        and isinstance(speed_kmh, (int, float))
        and not isinstance(speed_kmh, bool)
        and speed_kmh > 0
    ):
        eta = format_utc_and_manila_time(datetime.now(timezone.utc) + timedelta(seconds=nav_distance / (speed_kmh / 3.6)))

    return {
        "application_version": app_version(),
        "game": game,
        "game_name": _game_title(game),
        "game_version": game.get("version") or "--",
        "telemetry_api_version": game.get("telemetry_api_version") or "--",
        "connected": connected,
        "state": state,
        "worker_status": worker_status,
        "last_error": last_error or "--",
        "packets": agent.packets_received,
        "packets_per_second": agent.packets_per_second(now),
        "last_telemetry": agent.last_telemetry_timestamp,
        "session_id": agent.session_id or agent.last_session_id or "--",
        "session_start": agent.session_started_at or agent.last_session_started_at,
        "session_ended": agent.last_session_ended_at,
        "session_elapsed": metrics["duration_seconds"],
        "driving_time": metrics["driving_time_seconds"],
        "paused_time": max(0.0, metrics["duration_seconds"] - metrics["driving_time_seconds"]),
        "session_distance": metrics["distance_driven_km"],
        "truck": truck,
        "truck_configuration": truck_config,
        "job": job,
        "job_status": "Active" if job else "No Active Job",
        "real_time": format_utc_and_manila_time(),
        "real_eta": eta,
        "game_time": packet.get("game_time"),
        "in_game_elapsed": "--",
        "in_game_eta": "--",
        "latest_telemetry": packet,
    }


class TelemetryWorker(threading.Thread):
    def __init__(self, port: int, output_dir: Path = DEFAULT_OUTPUT_DIR) -> None:
        super().__init__(name="NLSI Telemetry UDP worker", daemon=False)
        self.port = port
        self.output_dir = output_dir
        self.updates: queue.Queue[dict[str, Any]] = queue.Queue()
        self.commands: queue.Queue[dict[str, Any]] = queue.Queue()
        self.stop_event = threading.Event()
        self.status = "Starting"
        self.last_error: str | None = None

    def _publish(self, message: dict[str, Any]) -> None:
        self.updates.put_nowait(message)

    def request_export(self, job: dict[str, Any], output_path: Path) -> None:
        self.commands.put({"type": "export_pdf", "job": copy.deepcopy(job), "output_path": output_path})

    def stop(self) -> None:
        self.stop_event.set()

    def _process_commands(self, agent: TelemetryAgent) -> None:
        while True:
            try:
                command = self.commands.get_nowait()
            except queue.Empty:
                return
            if command.get("type") == "export_pdf":
                try:
                    path = agent._write_pdf_report(command["job"], command["output_path"])
                except (OSError, TypeError, ValueError) as error:
                    self._publish({"type": "pdf_export", "error": str(error)})
                else:
                    self._publish({"type": "pdf_export", "path": str(path)})

    def run(self) -> None:
        agent = TelemetryAgent(self.output_dir, output=lambda _message: None)
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
                listener.bind(("127.0.0.1", self.port))
                listener.settimeout(0.1)
                self.status = "Listening"
                next_publish = 0.0
                next_history_publish = 0.0
                history_signature: tuple[int, int] | None = None
                while not self.stop_event.is_set():
                    try:
                        payload, _address = listener.recvfrom(65535)
                    except socket.timeout:
                        agent.check_timeout()
                    except OSError as error:
                        if not self.stop_event.is_set():
                            self.last_error = f"Telemetry socket error: {error}"
                        break
                    else:
                        try:
                            message = json.loads(payload.decode("utf-8"))
                            agent.process_message(message)
                            self.last_error = None
                        except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
                            self.last_error = f"Rejected telemetry packet: {error}"

                    self._process_commands(agent)
                    now = time.monotonic()
                    if now >= next_publish:
                        self._publish(make_snapshot(agent, self.last_error, self.status))
                        next_publish = now + 0.25
                    if now >= next_history_publish:
                        try:
                            events_path = agent.events_path
                            try:
                                stat = events_path.stat()
                                current_signature = (stat.st_mtime_ns, stat.st_size)
                            except FileNotFoundError:
                                current_signature = (0, 0)
                            if current_signature != history_signature:
                                records = read_event_records(events_path)
                                self._publish({"type": "history", "records": records})
                                history_signature = current_signature
                        except (OSError, ValueError) as error:
                            self._publish({"type": "history_error", "error": str(error)})
                        next_history_publish = now + 1.0
        except OSError as error:
            self.status = "Failed"
            self.last_error = f"Could not listen on 127.0.0.1:{self.port}: {error}"
            self._publish(make_snapshot(agent, self.last_error, self.status))
        finally:
            try:
                agent.close()
            except OSError as error:
                self.last_error = f"Could not finalize session logs: {error}"
            self.status = "Stopped"
            self._publish(make_snapshot(agent, self.last_error, self.status))


class NLSITelemetryApp:
    def __init__(self, root: tk.Tk, port: int) -> None:
        self.root = root
        self.worker = TelemetryWorker(port)
        self.snapshot: dict[str, Any] = {}
        self.job_rows: dict[str, dict[str, Any]] = {}
        self.last_history_error: str | None = None
        self.debug_cleared_at_packets: int | None = None
        self._build_window()
        self._build_tabs()
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.worker.start()
        self.root.after(100, self._poll_worker)
        self.root.after(1000, self._refresh_clock)

    def _build_window(self) -> None:
        self.root.title(f"{app_product()} | {app_release_label()}")
        self.root.geometry("1120x780")
        self.root.minsize(900, 640)
        icon_path = ROOT / "img" / "logo.ico"
        if icon_path.is_file():
            self.root.iconbitmap(str(icon_path))
        style = ttk.Style(self.root)
        if "vista" in style.theme_names():
            style.theme_use("vista")
        style.configure("AppTitle.TLabel", font=("Segoe UI", 17, "bold"))
        style.configure("Card.TLabelframe", padding=10)
        style.configure("Card.TLabelframe.Label", font=("Segoe UI", 10, "bold"))
        style.configure("Value.TLabel", font=("Segoe UI", 10))
        style.configure("Status.TLabel", font=("Segoe UI", 10, "bold"))
        header = ttk.Frame(self.root, padding=(18, 12, 18, 8))
        header.pack(fill="x")
        ttk.Label(header, text=f"{app_product()} | {app_release_label()}", style="AppTitle.TLabel").pack(side="left")
        self.header_status = ttk.Label(header, text="Starting telemetry worker...", style="Status.TLabel")
        self.header_status.pack(side="right")
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill="both", expand=True, padx=14, pady=(0, 14))

    def _add_tab(self, name: str) -> ttk.Frame:
        frame = ttk.Frame(self.notebook, padding=14)
        self.notebook.add(frame, text=name)
        return frame

    def _build_tabs(self) -> None:
        self.main_tab = self._add_tab("Main")
        self.jobs_tab = self._add_tab("Finished Jobs")
        self.pdf_tab = self._add_tab("PDF Export")
        self.debug_tab = self._add_tab("Debug")
        self.events_tab = self._add_tab("Events")
        self.mods_tab = self._add_tab("Active Mods")
        self.about_tab = self._add_tab("About Us")
        self._build_main_tab()
        self._build_jobs_tab()
        self._build_pdf_tab()
        self._build_debug_tab()
        self._build_events_tab()
        self._build_mods_tab()
        self._build_about_tab()

    def _make_card(
        self,
        parent: ttk.Frame,
        title: str,
        fields: tuple[str, ...],
        row: int,
        column: int,
        columnspan: int = 1,
    ) -> dict[str, ttk.Label]:
        card = ttk.LabelFrame(parent, text=title, style="Card.TLabelframe")
        card.grid(row=row, column=column, columnspan=columnspan, sticky="nsew", padx=6, pady=6)
        values: dict[str, ttk.Label] = {}
        for index, field in enumerate(fields):
            ttk.Label(card, text=f"{field}:", anchor="w").grid(row=index, column=0, sticky="nw", padx=(0, 12), pady=3)
            value = ttk.Label(card, text="--", style="Value.TLabel", anchor="w", wraplength=330)
            value.grid(row=index, column=1, sticky="ew", pady=3)
            values[field] = value
        card.columnconfigure(1, weight=1)
        return values

    def _build_main_tab(self) -> None:
        self.main_tab.columnconfigure(0, weight=1)
        self.main_tab.rowconfigure(0, weight=1)
        self.main_canvas = tk.Canvas(self.main_tab, highlightthickness=0)
        self.main_canvas.grid(row=0, column=0, sticky="nsew")
        self.main_scrollbar = ttk.Scrollbar(
            self.main_tab,
            orient="vertical",
            command=self.main_canvas.yview,
        )
        self.main_scrollbar.grid(row=0, column=1, sticky="ns")
        self.main_canvas.configure(yscrollcommand=self.main_scrollbar.set)
        self.main_content = ttk.Frame(self.main_canvas)
        self.main_content.columnconfigure(0, weight=1, uniform="main")
        self.main_content.columnconfigure(1, weight=1, uniform="main")
        self.main_content_window = self.main_canvas.create_window(
            (0, 0),
            anchor="nw",
            window=self.main_content,
        )
        self.main_canvas.bind("<Configure>", self._resize_main_content)
        self.main_content.bind("<Configure>", self._update_main_scrollregion)
        self.root.bind_all("<MouseWheel>", self._on_main_mousewheel, add="+")
        self.root.bind_all("<Button-4>", self._on_main_mousewheel, add="+")
        self.root.bind_all("<Button-5>", self._on_main_mousewheel, add="+")

        self.main_values: dict[str, ttk.Label] = {}
        self.main_values.update(
            self._make_card(
                self.main_content,
                "Connection",
                ("Game", "Game Version", "Telemetry API", "Plugin", "Last Telemetry", "Packets", "Packets/sec"),
                0,
                0,
            )
        )
        self.main_values.update(
            self._make_card(
                self.main_content,
                "Session",
                ("Session ID", "Session Start", "Session Ended", "Current Time", "Elapsed", "Driving Time", "Paused Time", "Session Distance", "Session Status"),
                0,
                1,
            )
        )
        self.main_values.update(
            self._make_card(
                self.main_content,
                "Truck",
                (
                    "Truck",
                    "Plate",
                    "Speed",
                    "RPM",
                    "Gear",
                    "Steering",
                    "Throttle",
                    "Brake",
                    "Retarder",
                    "Cruise Control",
                    "Adaptive Cruise Control",
                    "Fuel",
                    "Fuel Range",
                    "Odometer",
                ),
                1,
                0,
            )
        )
        self.main_values.update(
            self._make_card(
                self.main_content,
                "Job",
                (
                    "Job Status",
                    "Job ID",
                    "Cargo",
                    "Source",
                    "Destination",
                    "Job Distance",
                    "Remaining",
                    "Deadline",
                    "Revenue",
                    "Job Type",
                    "Trailer",
                ),
                1,
                1,
            )
        )
        self.main_values.update(
            self._make_card(
                self.main_content,
                "Time",
                ("Real Time", "Real ETA", "Game Time", "In-Game Elapsed", "In-Game ETA"),
                2,
                0,
                columnspan=2,
            )
        )

    def _resize_main_content(self, event: tk.Event) -> None:
        self.main_canvas.itemconfigure(self.main_content_window, width=event.width)

    def _update_main_scrollregion(self, _event: tk.Event) -> None:
        self.main_canvas.configure(scrollregion=self.main_canvas.bbox("all"))

    def _on_main_mousewheel(self, event: tk.Event) -> str | None:
        if self.notebook.select() != str(self.main_tab):
            return None
        if not str(event.widget).startswith(str(self.main_canvas)):
            return None

        if event.num == 4:
            direction = -1
        elif event.num == 5:
            direction = 1
        else:
            delta = event.delta
            direction = -int(delta / 120) if abs(delta) >= 120 else (-1 if delta > 0 else 1)
        self.main_canvas.yview_scroll(direction, "units")
        return "break"

    def _build_jobs_tab(self) -> None:
        toolbar = ttk.Frame(self.jobs_tab)
        toolbar.pack(fill="x", pady=(0, 8))
        ttk.Label(toolbar, text="Status:").pack(side="left", padx=(0, 6))
        self.job_filter = tk.StringVar(value="All")
        filter_box = ttk.Combobox(toolbar, textvariable=self.job_filter, values=("All", "Delivered", "Pending", "Cancelled"), state="readonly", width=14)
        filter_box.pack(side="left")
        filter_box.bind("<<ComboboxSelected>>", lambda _event: self._render_jobs())
        columns = ("job_id", "status", "game", "cargo", "source", "destination", "distance", "revenue", "truck", "plate", "start", "end", "session")
        self.jobs_tree = ttk.Treeview(self.jobs_tab, columns=columns, show="headings", height=15)
        headings = (
            ("job_id", "Job ID", 95),
            ("status", "Status", 80),
            ("game", "Game", 65),
            ("cargo", "Cargo", 130),
            ("source", "Source", 100),
            ("destination", "Destination", 100),
            ("distance", "Distance", 85),
            ("revenue", "Revenue", 90),
            ("truck", "Truck", 100),
            ("plate", "Plate", 85),
            ("start", "Start Time", 145),
            ("end", "End Time", 145),
            ("session", "Session ID", 150),
        )
        for column, title, width in headings:
            self.jobs_tree.heading(column, text=title)
            self.jobs_tree.column(column, width=width, minwidth=60, stretch=True)
        scroll = ttk.Scrollbar(self.jobs_tab, orient="vertical", command=self.jobs_tree.yview)
        self.jobs_tree.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.jobs_tree.pack(fill="both", expand=True)
        self.jobs_tree.bind("<<TreeviewSelect>>", self._show_job_details)
        self.job_details = ttk.Label(self.jobs_tab, text="Select a job to view its persisted details.", anchor="w", justify="left", wraplength=1000)
        self.job_details.pack(fill="x", pady=(10, 0))
        self.job_status_label = ttk.Label(self.jobs_tab, text="No persisted job events yet.", anchor="w")
        self.job_status_label.pack(fill="x", pady=(6, 0))

    def _build_pdf_tab(self) -> None:
        ttk.Label(self.pdf_tab, text="Export a report for a persisted job. PDF files are created only after you select a job and choose Export PDF.", wraplength=760).pack(anchor="w", pady=(0, 16))
        row = ttk.Frame(self.pdf_tab)
        row.pack(fill="x", pady=6)
        ttk.Label(row, text="Completed job:").pack(side="left", padx=(0, 8))
        self.pdf_job_value = tk.StringVar()
        self.pdf_job_box = ttk.Combobox(row, textvariable=self.pdf_job_value, state="readonly", width=80)
        self.pdf_job_box.pack(side="left", fill="x", expand=True)
        self.export_button = ttk.Button(self.pdf_tab, text="Export PDF", command=self._export_pdf, state="disabled")
        self.export_button.pack(anchor="w", pady=(12, 8))
        self.pdf_status = ttk.Label(self.pdf_tab, text="Select a delivered job to export.")
        self.pdf_status.pack(anchor="w")

    def _build_debug_tab(self) -> None:
        controls = ttk.Frame(self.debug_tab)
        controls.pack(fill="x", pady=(0, 8))
        ttk.Button(controls, text="Clear Debug Output", command=self._clear_debug).pack(side="left")
        ttk.Button(controls, text="Copy Debug Information", command=self._copy_debug).pack(side="left", padx=8)
        self.debug_text = tk.Text(self.debug_tab, wrap="none", height=28, state="disabled", font=("Consolas", 9))
        y_scroll = ttk.Scrollbar(self.debug_tab, orient="vertical", command=self.debug_text.yview)
        x_scroll = ttk.Scrollbar(self.debug_tab, orient="horizontal", command=self.debug_text.xview)
        self.debug_text.configure(yscrollcommand=y_scroll.set, xscrollcommand=x_scroll.set)
        y_scroll.pack(side="right", fill="y")
        x_scroll.pack(side="bottom", fill="x")
        self.debug_text.pack(fill="both", expand=True)

    def _build_events_tab(self) -> None:
        columns = ("time", "event", "game", "session", "details")
        self.events_tree = ttk.Treeview(self.events_tab, columns=columns, show="headings")
        for column, title, width in (
            ("time", "Time", 160),
            ("event", "Event", 190),
            ("game", "Game", 90),
            ("session", "Session ID", 180),
            ("details", "Details", 560),
        ):
            self.events_tree.heading(column, text=title)
            self.events_tree.column(column, width=width, minwidth=70, stretch=True)
        scroll = ttk.Scrollbar(self.events_tab, orient="vertical", command=self.events_tree.yview)
        self.events_tree.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.events_tree.pack(fill="both", expand=True)
        self.events_status = ttk.Label(self.events_tab, text="Loading persisted events...", anchor="w")
        self.events_status.pack(fill="x", pady=(8, 0))

    def _build_mods_tab(self) -> None:
        ttk.Label(
            self.mods_tab,
            text="Active mod detection is currently unavailable through the official telemetry interface.",
            wraplength=760,
            justify="left",
        ).pack(anchor="w", padx=12, pady=12)

    def _build_about_tab(self) -> None:
        panel = ttk.Frame(self.about_tab, padding=24)
        panel.pack(anchor="nw", fill="x")
        ttk.Label(panel, text=f"{app_product()} | {app_release_label()}", style="AppTitle.TLabel").pack(anchor="w", pady=(0, 12))
        ttk.Label(panel, text="Nabski Logistics and Solutions Inc.\nNLSI\nKamote Hauling\n\nETS2 / ATS telemetry companion for NLSI drivers.", justify="left").pack(anchor="w")
        self.about_version = ttk.Label(panel, text=f"Application Version: {app_version()} ({app_release_label()})")
        self.about_version.pack(anchor="w", pady=(14, 8))
        self.social_frame = ttk.Frame(panel)
        self.social_frame.pack(anchor="w", pady=(8, 0))
        ttk.Button(
            self.social_frame,
            text="Project Repository",
            command=lambda: webbrowser.open(PROJECT_URL),
        ).pack(side="left", padx=(0, 8))
        env_values = parse_env_urls((ROOT / ".env", ROOT.parent / "config" / ".env"))
        for key, title in SOCIAL_KEYS.items():
            url = env_values.get(key)
            if url:
                ttk.Button(self.social_frame, text=title, command=lambda configured_url=url: webbrowser.open(configured_url)).pack(side="left", padx=(0, 8))

    def _poll_worker(self) -> None:
        latest: dict[str, Any] | None = None
        while True:
            try:
                latest = self.worker.updates.get_nowait()
            except queue.Empty:
                break
            if latest.get("type") == "pdf_export":
                self._handle_export_result(latest)
            elif latest.get("type") == "history":
                self.last_history_error = None
                self._render_events(latest["records"])
                self._render_jobs(jobs_from_events(latest["records"]))
            elif latest.get("type") == "history_error":
                self.last_history_error = str(latest["error"])
                self.job_status_label.configure(text=f"Could not load persisted history: {self.last_history_error}")
                self.events_status.configure(text=f"Could not load persisted history: {self.last_history_error}")
            else:
                self.snapshot = latest
        if self.snapshot:
            self._render_snapshot()
        if self.root.winfo_exists():
            self.root.after(100, self._poll_worker)

    def _set_value(self, key: str, value: Any) -> None:
        label = self.main_values.get(key)
        if label is not None:
            label.configure(text=str(value) if value not in (None, "") else "--")

    def _render_snapshot(self) -> None:
        data = self.snapshot
        state = data.get("state", "Idle")
        self.header_status.configure(text=f"{state}  |  Telemetry worker: {data.get('worker_status', 'Starting')}")
        self._set_value("Game", data.get("game_name"))
        self._set_value("Game Version", data.get("game_version"))
        self._set_value("Telemetry API", data.get("telemetry_api_version"))
        self._set_value("Plugin", "Connected" if data.get("connected") else "Disconnected")
        self._set_value("Last Telemetry", format_clock(data.get("last_telemetry")))
        self._set_value("Packets", data.get("packets"))
        pps = data.get("packets_per_second")
        self._set_value("Packets/sec", display_number(pps) if pps is not None else "--")
        self._set_value("Session ID", data.get("session_id"))
        self._set_value("Session Start", format_real_time(data.get("session_start")))
        self._set_value("Session Ended", format_real_time(data.get("session_ended")))
        self._set_value("Current Time", data.get("real_time"))
        self._set_value("Elapsed", display_duration(data.get("session_elapsed")))
        self._set_value("Driving Time", display_duration(data.get("driving_time")))
        self._set_value("Paused Time", display_duration(data.get("paused_time")))
        self._set_value("Session Distance", f"{display_number(data.get('session_distance'))} km")
        self._set_value("Session Status", state)

        truck = data.get("truck", {})
        truck_config = data.get("truck_configuration", {})
        speed = truck.get("speed_kmh")
        if speed is None and isinstance(truck.get("speed_mps"), (int, float)):
            speed = truck["speed_mps"] * 3.6
        fuel_capacity = _first_value(truck_config, "fuel_capacity_liters", "fuel_capacity_l", "fuel_capacity")
        self._set_value("Truck", " ".join(str(value) for value in (
            _first_value(truck_config, "brand", "manufacturer"),
            _first_value(truck_config, "name", "model", "model_name"),
        ) if value) or "--")
        self._set_value("Plate", _first_value(truck_config, "license_plate", "licenseplate", "plate"))
        self._set_value("Speed", f"{display_number(speed)} km/h")
        self._set_value("RPM", display_number(truck.get("rpm"), 0))
        self._set_value("Gear", display_gear_value(truck))
        self._set_value("Steering", display_number(truck.get("steering") * 100, 0, "%") if isinstance(truck.get("steering"), (int, float)) else "--")
        self._set_value("Throttle", display_throttle_value(truck))
        self._set_value("Brake", display_brake_value(truck))
        self._set_value("Retarder", display_retarder_value(truck))
        self._set_value("Cruise Control", display_cruise_value(truck))
        self._set_value("Adaptive Cruise Control", display_adaptive_cruise_value(truck))
        self._set_value("Fuel", f"{display_number(truck.get('fuel_liters'))} / {display_number(fuel_capacity)} L")
        self._set_value("Fuel Range", f"{display_number(truck.get('fuel_range_km'))} km")
        self._set_value("Odometer", f"{display_number(truck.get('odometer_km'))} km")

        job = data.get("job", {})
        self._set_value("Job Status", data.get("job_status"))
        self._set_value("Job ID", _first_value(job, "job_id", "id"))
        self._set_value("Cargo", _first_value(job, "cargo", "cargo_name"))
        self._set_value("Source", _first_value(job, "source", "source_city", "source_city_name"))
        self._set_value("Destination", _first_value(job, "destination", "destination_city", "destination_city_name"))
        distance = _first_value(job, "planned_distance_km", "planned_distance", "distance_km")
        remaining = _first_value(job, "remaining_distance_km", "remaining_distance")
        self._set_value("Job Distance", f"{display_number(distance)} km" if distance is not None else "-- km")
        self._set_value("Remaining", f"{display_number(remaining)} km")
        self._set_value("Deadline", _first_value(job, "delivery_time", "delivery_deadline"))
        self._set_value("Revenue", _first_value(job, "income", "job_income", "revenue"))
        self._set_value("Job Type", _first_value(job, "job_type", "type"))
        trailer = data.get("latest_telemetry", {}).get("trailer", {})
        self._set_value("Trailer", display_trailer_value(trailer))
        self._set_value("Real Time", data.get("real_time"))
        self._set_value("Real ETA", data.get("real_eta"))
        self._set_value("Game Time", data.get("game_time"))
        self._set_value("In-Game Elapsed", data.get("in_game_elapsed"))
        self._set_value("In-Game ETA", data.get("in_game_eta"))
        self._render_debug()

    def _render_debug(self) -> None:
        current_packets = self.snapshot.get("packets")
        if self.debug_cleared_at_packets is not None and current_packets == self.debug_cleared_at_packets:
            return
        self.debug_cleared_at_packets = None
        summary = {
            "Application Version": self.snapshot.get("application_version"),
            "Telemetry API Version": self.snapshot.get("telemetry_api_version"),
            "SCS SDK Contract": "1.01",
            "Game": self.snapshot.get("game"),
            "Plugin Status": self.snapshot.get("worker_status"),
            "Connection Status": self.snapshot.get("state"),
            "Packets Received": current_packets,
            "Packets/sec": self.snapshot.get("packets_per_second"),
            "Last Telemetry Timestamp": self.snapshot.get("last_telemetry"),
            "Session ID": self.snapshot.get("session_id"),
            "Thread/Agent Status": self.snapshot.get("worker_status"),
            "Last Error": self.snapshot.get("last_error"),
            "Latest Telemetry": redact_sensitive(self.snapshot.get("latest_telemetry", {})),
        }
        self.debug_text.configure(state="normal")
        self.debug_text.delete("1.0", "end")
        self.debug_text.insert("1.0", json.dumps(summary, ensure_ascii=False, indent=2))
        self.debug_text.configure(state="disabled")

    def _clear_debug(self) -> None:
        self.debug_cleared_at_packets = self.snapshot.get("packets")
        self.debug_text.configure(state="normal")
        self.debug_text.delete("1.0", "end")
        self.debug_text.configure(state="disabled")

    def _copy_debug(self) -> None:
        try:
            content = self.debug_text.get("1.0", "end-1c")
            self.root.clipboard_clear()
            self.root.clipboard_append(content)
        except tk.TclError as error:
            messagebox.showerror("Copy failed", str(error), parent=self.root)

    def _render_events(self, records: list[dict[str, Any]]) -> None:
        children = self.events_tree.get_children()
        if children:
            self.events_tree.delete(*children)
        for record in records[-1000:]:
            data = record.get("data") if isinstance(record.get("data"), dict) else {}
            session_id = data.get("session_id") or (data.get("session_id") if record.get("event") == "SESSION_STARTED" else None) or "--"
            details = json.dumps(redact_sensitive(data), ensure_ascii=False, separators=(",", ":"))
            self.events_tree.insert(
                "",
                "end",
                values=(format_clock(record.get("timestamp")), record.get("event", "--"), _game_name(record), session_id, details),
            )
        self.events_status.configure(text=f"{len(records)} persisted event records.")

    def _render_jobs(self, jobs: list[dict[str, Any]] | None = None) -> None:
        if jobs is not None:
            self.job_rows = {str(job["record_index"]): job for job in jobs}
        selected_filter = self.job_filter.get()
        visible = filter_jobs(list(self.job_rows.values()), selected_filter)
        existing = self.jobs_tree.get_children()
        if existing:
            self.jobs_tree.delete(*existing)
        for job in visible:
            details = job.get("details", {})
            row_id = str(job["record_index"])
            self.jobs_tree.insert(
                "",
                "end",
                iid=row_id,
                values=(
                    job.get("job_id") or "--",
                    job.get("status") or "--",
                    job.get("game") or "--",
                    _first_value(details, "cargo", "cargo_name") or "--",
                    _first_value(details, "source", "source_city", "source_city_name") or "--",
                    _first_value(details, "destination", "destination_city", "destination_city_name") or "--",
                    display_number(_first_value(details, "distance_km", "planned_distance_km", "planned_distance")),
                    _first_value(details, "revenue", "income", "job_income") or "--",
                    _first_value(details, "truck_model", "truck") or "--",
                    _first_value(details, "license_plate", "plate") or "--",
                    format_real_time(job.get("start_time")),
                    format_real_time(job.get("end_time")),
                    job.get("session_id") or "--",
                ),
            )
        delivered = [job for job in self.job_rows.values() if job.get("status") == "Delivered"]
        self.job_status_label.configure(text=f"{len(self.job_rows)} persisted jobs; {len(delivered)} delivered.")
        pdf_values = [
            f"{job.get('session_id') or '--'} | {job.get('job_id') or '--'} | "
            f"{_first_value(job.get('details', {}), 'cargo', 'cargo_name') or '--'} | "
            f"{_first_value(job.get('details', {}), 'source', 'source_city') or '--'} -> "
            f"{_first_value(job.get('details', {}), 'destination', 'destination_city') or '--'}"
            for job in delivered
        ]
        self.pdf_job_box.configure(values=pdf_values)
        self._pdf_jobs = delivered
        if self.pdf_job_value.get() not in pdf_values:
            self.pdf_job_value.set(pdf_values[0] if pdf_values else "")
        self.export_button.configure(state="normal" if pdf_values else "disabled")

    def _show_job_details(self, _event: tk.Event) -> None:
        selection = self.jobs_tree.selection()
        if not selection:
            return
        job = self.job_rows.get(selection[0])
        if not job:
            return
        self.job_details.configure(text=json.dumps(redact_sensitive(job), ensure_ascii=False, indent=2))

    def _export_pdf(self) -> None:
        selected = self.pdf_job_value.get()
        if not selected:
            return
        jobs = getattr(self, "_pdf_jobs", [])
        index = next((idx for idx, job in enumerate(jobs) if selected.startswith(f"{job.get('session_id') or '--'} | {job.get('job_id') or '--'} |")), None)
        if index is None:
            self.pdf_status.configure(text="The selected job is no longer available. Refresh the job history and try again.")
            return
        job = jobs[index]
        safe_job_id = str(job.get("job_id") or job.get("session_id") or "job").replace(":", "-").replace("\\", "-").replace("/", "-")
        output_path = filedialog.asksaveasfilename(
            parent=self.root,
            title="Export job report",
            defaultextension=".pdf",
            initialfile=f"NLSI-{safe_job_id}.pdf",
            filetypes=(("PDF files", "*.pdf"),),
        )
        if not output_path:
            return
        details = dict(job.get("details", {}))
        details.update(
            {
                "job_id": job.get("job_id"),
                "session_id": job.get("session_id"),
                "start_time": job.get("start_time"),
                "end_time": job.get("end_time"),
                "status": job.get("status"),
            }
        )
        self.pdf_status.configure(text=f"Exporting to {output_path}...")
        self.export_button.configure(state="disabled")
        self.worker.request_export(details, Path(output_path))

    def _handle_export_result(self, result: dict[str, Any]) -> None:
        self.export_button.configure(state="normal" if getattr(self, "_pdf_jobs", []) else "disabled")
        if "error" in result:
            self.pdf_status.configure(text=f"Export failed: {result['error']}")
            messagebox.showerror("PDF export failed", result["error"], parent=self.root)
        else:
            self.pdf_status.configure(text=f"PDF exported: {result['path']}")

    def _refresh_clock(self) -> None:
        current_time = format_utc_and_manila_time()
        self._set_value("Current Time", current_time)
        self._set_value("Real Time", current_time)
        if self.root.winfo_exists():
            self.root.after(1000, self._refresh_clock)

    def close(self) -> None:
        self.worker.stop()
        if self.worker.is_alive():
            self.worker.join(timeout=2.0)
        self.root.destroy()


def run_gui(port: int = 28745) -> None:
    root = tk.Tk()
    NLSITelemetryApp(root, port)
    root.mainloop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=f"{app_product()} desktop interface")
    parser.add_argument("--port", type=int, default=28745, help="UDP port used by the local SCS plugin")
    arguments = parser.parse_args()
    if not 1 <= arguments.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    run_gui(arguments.port)
