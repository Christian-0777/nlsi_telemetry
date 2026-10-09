# NLSI native log formats

The native application keeps the existing UTF-8 `.txt` and v1 `.nlsi`
application log, and writes revision-13 telemetry into separate v2 `.nlsi`
files. Both live below
`%LOCALAPPDATA%\NLSI\Exclusive Logbook`; existing logs and history are never
overwritten by telemetry capture or removed during upgrade/uninstall.

## Existing application log, schema version 1

## Version 1

`.nlsi` is newline-delimited JSON (one complete JSON object per UTF-8 line).
The first line is a required format header:

```json
{"format":"nlsi-log","schema_version":1,"record_type":"header"}
```

Subsequent lines are log entries:

```json
{"record_type":"entry","timestamp":"2026-10-09T04:45:37.198Z","message":"[startup] NLSI Exclusive Logbook v1.4.2-beta"}
```

`timestamp` is an ISO 8601 UTC timestamp with millisecond precision. `message`
is the original log message as UTF-8 JSON text; embedded newlines are escaped
within the JSON string. Application messages and all numeric values remain
unaltered in storage.

Writers validate the existing header and all records before appending to an
existing file. Unknown schema versions, malformed records, and incomplete
trailing lines are reported as errors; the file is not truncated, repaired, or
silently replaced. New records are appended and flushed. Readers reject an
invalid file without returning a partial record list. TXT writing continues
alongside `.nlsi` writing and retains the prior line-oriented format.

## Telemetry logs, schema version 2

One file is opened per Asia/Manila calendar day at
`%LOCALAPPDATA%\NLSI\Exclusive Logbook\telemetry\YYYY-MM-DD.nlsi`. A file
rotates at 128 MiB to `YYYY-MM-DD-NNN.nlsi`; rotation creates another file and
does not remove earlier data. The header declares `format=nlsi-telemetry` and
`schema_version=2`. Each subsequent JSON Lines record contains a generated
stable record ID, monotonically increasing local sequence, driving-session ID
or `null`, capture UTC timestamp, provider/revision/mapping identity, source
timestamps, named raw fields and their availability, a compressed Base64 copy
of the complete 32 KiB mapping, and separately represented normalized fields.
No authenticated-account identifier or credential is stored.

Samples are queued to a background writer and appended on source timestamp
changes, polling the shared map every 250 ms. Each complete record is flushed
before its record ID is appended to `sync\queue.jsonl`. If the process stops
between those writes, startup scans local records and reconstructs missing
pending queue entries. A partial trailing line is copied byte-for-byte to a
`.recovery` sidecar before the incomplete suffix is removed; preceding complete
records are retained. A malformed complete record or unknown schema is
preserved and reported rather than silently rewritten.

The queue remains `Pending`/local-only until an authenticated HTTPS API,
account mechanism, and server-side database are supplied. This alpha performs
no network upload and never marks records synced. Disk-full, queue-full, and
write failures are surfaced in Settings → Providers; capture failure does not
crash the shared-memory reader. Users should keep sufficient free disk space
and archive old files themselves only after making their own backup. There is
no automatic deletion or retention policy.

Files can be inspected with any UTF-8 text editor or JSON Lines viewer. The
v2 record’s `raw_mapping_base64` decodes to `qCompress` data containing the
exact 32 KiB mapping; `raw_fields` is the named revision-13 subset NLSI
currently decodes, while `normalized_fields` carries application values and
their availability/source/timestamp/stale metadata. The legacy
`Logger::ReadNlsiLog` reader reads v1 application logs; it intentionally does
not reinterpret v2 telemetry records. TXT logs continue to contain the
existing human-readable application/history output.

Before a queued sample reaches its daily `.nlsi` file, it is stored atomically
under `telemetry\pending\<record-id>.json` using the internal
`nlsi-pending-sample` schema version 1. The writer removes that recovery copy
only after the v2 record and its pending-sync entry have been flushed. Startup
reconciles a recovery copy against existing record IDs to avoid duplicating a
record if interruption happened between those writes. The UTC `timestamp_utc`
field remains UTC and unchanged; only daily file grouping and local display
use the IANA `Asia/Manila` zone. OS termination, storage-device failure, and
power loss can still exceed the guarantees of application-level flushing.
