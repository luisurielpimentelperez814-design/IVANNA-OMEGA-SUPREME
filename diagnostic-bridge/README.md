# IVANNA Live Diagnostic Bridge

Small standalone Node.js 20+ service for authenticated, privacy-minimized health snapshots.

## Deploy

1. Deploy this directory as a separate service behind a platform-provided HTTPS endpoint (Cloud Run, Fly.io, Render, etc.).
2. Set `IVANNA_DIAGNOSTIC_TOKEN` to a randomly generated secret of at least 32 characters. Never commit it or bake it into the APK.
3. Set `PORT` if the host requires it. Health check: `GET /healthz`.
4. Configure IVANNA's local diagnostic connector with the HTTPS base URL and the same bearer token. Enable uploads explicitly in app settings.
5. Restrict access to the latest snapshot with the same token. Only one latest snapshot is held in memory; it is lost on restart. No database, raw audio, IP logging, or personal device identifier is stored by application code.

## API

- `POST /v1/diagnostics` — bearer-authenticated JSON snapshot, max 16 KiB, 60 requests/min/IP.
- `GET /v1/diagnostics/latest` — bearer-authenticated latest snapshot.
- `GET /healthz` — health only; contains no device data.

The server allowlists fields and rejects arbitrary strings/extra data by reconstructing the snapshot. Deploy behind HTTPS; do not expose plain HTTP on the public internet. The current implementation is a single-instance diagnostic relay, not a durable queue or a direct ChatGPT connection. To let an AI assistant analyze the data, an authorized client must retrieve the snapshot and explicitly provide it to that assistant. No repair command endpoint exists by design.
