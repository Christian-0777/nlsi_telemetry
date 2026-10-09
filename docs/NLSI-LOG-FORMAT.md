# NLSI native log format

The native application writes both the existing UTF-8 `.txt` log and a
structured UTF-8 `.nlsi` log under the current user's application data
directory. Existing `.txt`, event, session, and job files are preserved during
upgrades and remain readable; the `.nlsi` stream is an additional format.

## Version 1

`.nlsi` is newline-delimited JSON (one complete JSON object per UTF-8 line).
The first line is a required format header:

```json
{"format":"nlsi-log","schema_version":1,"record_type":"header"}
```

Subsequent lines are log entries:

```json
{"record_type":"entry","timestamp":"2026-10-09T04:45:37.198Z","message":"[startup] NLSI Exclusive Logbook v1.3.8 Alpha"}
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
