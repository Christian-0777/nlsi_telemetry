-- NLSI Exclusive Logbook alpha telemetry schema design.
-- Target: MySQL 8.0.16+ (CHECK constraints enforced).
-- UTC convention: every *_utc DATETIME(3) value is UTC; clients convert only
-- at presentation time. No account table is defined because this application
-- has no authenticated account system.

CREATE TABLE driving_sessions (
    session_id VARCHAR(64) NOT NULL,
    game_code VARCHAR(16) NULL,
    started_at_utc DATETIME(3) NOT NULL,
    ended_at_utc DATETIME(3) NULL,
    end_reason VARCHAR(48) NULL,
    PRIMARY KEY (session_id),
    KEY ix_driving_sessions_started (started_at_utc),
    CONSTRAINT ck_driving_sessions_time
        CHECK (ended_at_utc IS NULL OR ended_at_utc >= started_at_utc)
) ENGINE=InnoDB;

CREATE TABLE telemetry_samples (
    record_id CHAR(36) NOT NULL,
    record_schema_version TINYINT UNSIGNED NOT NULL,
    session_id VARCHAR(64) NULL,
    sequence_no BIGINT UNSIGNED NOT NULL,
    captured_at_utc DATETIME(3) NOT NULL,
    provider_name VARCHAR(64) NOT NULL,
    provider_revision SMALLINT UNSIGNED NOT NULL,
    source_timestamps JSON NOT NULL,
    raw_fields JSON NOT NULL,
    raw_availability JSON NOT NULL,
    raw_mapping MEDIUMBLOB NOT NULL,
    normalized_fields JSON NOT NULL,
    received_at_utc DATETIME(3) NOT NULL,
    PRIMARY KEY (record_id),
    UNIQUE KEY uq_telemetry_session_sequence (session_id, sequence_no),
    KEY ix_telemetry_captured (captured_at_utc),
    KEY ix_telemetry_session_time (session_id, captured_at_utc),
    CONSTRAINT fk_telemetry_session
        FOREIGN KEY (session_id) REFERENCES driving_sessions (session_id)
        ON DELETE RESTRICT ON UPDATE RESTRICT,
    CONSTRAINT ck_telemetry_revision CHECK (provider_revision > 0),
    CONSTRAINT ck_telemetry_schema CHECK (record_schema_version = 2),
    CONSTRAINT ck_telemetry_raw_mapping CHECK (OCTET_LENGTH(raw_mapping) <= 65536)
) ENGINE=InnoDB;

CREATE TABLE job_records (
    record_id CHAR(36) NOT NULL,
    session_id VARCHAR(64) NULL,
    job_identity VARCHAR(128) NULL,
    event_type VARCHAR(32) NOT NULL,
    event_at_utc DATETIME(3) NOT NULL,
    details JSON NOT NULL,
    PRIMARY KEY (record_id),
    KEY ix_job_session_time (session_id, event_at_utc),
    KEY ix_job_identity (job_identity),
    CONSTRAINT fk_job_session
        FOREIGN KEY (session_id) REFERENCES driving_sessions (session_id)
        ON DELETE RESTRICT ON UPDATE RESTRICT,
    CONSTRAINT ck_job_event_type
        CHECK (event_type IN ('job.started', 'job.delivered', 'job.cancelled'))
) ENGINE=InnoDB;

CREATE TABLE sync_state (
    record_type ENUM('telemetry_sample', 'job_record') NOT NULL,
    record_id CHAR(36) NOT NULL,
    state ENUM('pending', 'syncing', 'synced', 'error') NOT NULL,
    attempt_count INT UNSIGNED NOT NULL DEFAULT 0,
    next_attempt_at_utc DATETIME(3) NULL,
    acknowledged_at_utc DATETIME(3) NULL,
    last_error_code VARCHAR(64) NULL,
    updated_at_utc DATETIME(3) NOT NULL,
    PRIMARY KEY (record_type, record_id),
    KEY ix_sync_due (state, next_attempt_at_utc),
    KEY ix_sync_updated (updated_at_utc),
    CONSTRAINT ck_sync_ack_state
        CHECK (state <> 'synced' OR acknowledged_at_utc IS NOT NULL)
) ENGINE=InnoDB;

-- Referential validation for sync_state.record_id is performed in the same
-- server transaction against telemetry_samples or job_records, selected by
-- record_type. The generic queue key intentionally supports both record types.
