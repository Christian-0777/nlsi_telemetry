# Telemetry and synchronization SQL design

`db/migrations/001_telemetry_sync.sql` is a versioned MySQL 8.0.16+ design
migration created only after the source-field inventory in
[TELEMETRY-MAPPING.md](./TELEMETRY-MAPPING.md). It has not been applied: this
repository contains no backend, authentication, database connection, migration
runner, or online API.

## Entity/field mapping

| SQL entity/column | Local source | Notes |
|---|---|---|
| `driving_sessions.session_id` | `sessions.jsonl` `session_id` | Driving session only; not an authenticated-user session |
| `game_code`, `started_at_utc`, `ended_at_utc` | `sessions.jsonl` `game`, lifecycle timestamps | UTC `DATETIME(3)` convention |
| `telemetry_samples.record_id` | v2 `.nlsi` generated `record_id` | UUID stable across retries |
| `record_schema_version` | v2 `.nlsi` `schema_version` | Constrained to the version-2 sample schema |
| `session_id`, `sequence_no`, `captured_at_utc` | v2 `.nlsi` session, sequence, UTC capture timestamp | Session nullable when the game SDK session is not active/known |
| `provider_name`, `provider_revision` | v2 `.nlsi` provider metadata | TruckSim GPS and revision 13 |
| `source_timestamps` | v2 `.nlsi` source sample/simulation/render counters | Decimal strings preserve uint64 range; counter units are not claimed |
| `raw_fields`, `raw_availability` | v2 `.nlsi` raw decoded source fields and validity | Individual values remain queryable as JSON |
| `raw_mapping` | v2 `.nlsi` `raw_mapping_base64` | Base64-decode the qCompress payload; the column holds the compressed binary mapping |
| `normalized_fields` | v2 `.nlsi` `normalized_fields` | Separate from raw values; includes normalized value, availability, source, timestamp, and stale flag |
| `job_records.record_id`, `event_type`, `details` | Existing provider/job event and `jobs.jsonl` data | A future synchronization adapter must assign a stable record ID before transmission |
| `sync_state` | Local durable `sync/queue.jsonl` | Server model for pending/syncing/synced/error; local alpha currently writes only pending entries |
| Account/user columns | None | No account table or account ID is defined because no authenticated account system exists |

The migration includes session/sample/job foreign keys, UTC timestamp fields,
unique record IDs, query indexes, JSON source payloads, and a bounded raw
mapping. MySQL `sync_state` is a typed queue table; the service must validate
that `record_id` exists in the table selected by `record_type` inside the same
transaction. This polymorphic reference is documented rather than represented
as two nullable foreign keys that would incorrectly require a record to exist
in both tables.

## Applying and rolling back

When a backend is introduced, apply this migration once in a transaction
through its versioned migration runner after taking a database backup. Confirm
that the database is MySQL 8.0.16+ and that JSON and enforced CHECK constraints
are supported. Do not apply it directly from the desktop application. Verify
all tables, indexes, constraints, UTC handling, JSON query plans, and
sample/session/job foreign-key behavior in a disposable database before
production deployment. Record migration version `001_telemetry_sync` in the
backend's migration ledger.

Rollback is a separate, destructive administrative operation and must only be
performed after a verified database backup and an explicit retention decision.
In reverse dependency order, drop `sync_state`, `job_records`,
`telemetry_samples`, then `driving_sessions`. This removes server-side data;
it does not delete or alter any local `.nlsi`, TXT, job-history, or sync-queue
files. The application has no migration or rollback command.

## Synchronization/security status

No desktop API endpoint exists in this codebase. There is no claimed endpoint,
authentication provider, server-side prepared statement, per-account
authorization, TLS configuration, or end-to-end upload test. Consequently,
the alpha never uploads, never calls MySQL, never stores secrets, and never
marks a sample `synced`. Local queue records remain pending across restarts.
An eventual service must use authenticated HTTPS, authorize every batch
against the authenticated principal, enforce idempotency on `record_id`,
validate payload/record sizes and schemas, use prepared statements, retry
transient failures with bounded exponential backoff, and acknowledge only
after durable acceptance.
