# REST API reference

`halberd-full` exposes ~50 HTTP endpoints over its Wi-Fi AP at
`http://192.168.4.1`. This page is the endpoint-level reference:
path, method, query string, request body, response shape, status
codes. `halberd-headless` has no HTTP server — those nodes are
mesh-driven only.

> 🚧 **Stub page.** Content lands in a follow-up commit. The
> [README's API Reference section](../../README.md#api-reference)
> has the full route list in the meantime.

## What this page will cover

- **Core**: `/`, `/diag`, `/stop`, `/config`.
- **Scanning**: `/scan`, `/sniffer`, `/drone`.
- **Results**: `/results`, `/sniffer-cache`, `/probe-results`,
  `/deauth-results`, `/randomization-results`, `/baseline-results`,
  `/drone-results`, `/drone-log`.
- **SD data explorer**: `/api/probes.jsonl`, `/api/deauth.jsonl`,
  `/api/drones.jsonl`, `/api/vibrations.jsonl`, `/api/halberd.log`.
- **Configuration**: `/rf-config`, `/wifi-config`, `/baseline/*`,
  `/triangulate/*`, `/randomization/*`, `/erase/*`, `/secure/*`,
  `/config/autoerase`, `/battery-saver`, `/mesh*`, `/gps`,
  `/sd-status`.

For each endpoint:
- HTTP method (`GET` / `POST`).
- Path + path/query parameters.
- Request body (when applicable) with JSON schema.
- Response body with JSON schema.
- Status codes including the meaningful 4xx/5xx ones.
- Authentication / origin gating (currently none — see notes).

## Security notes

The API is currently unauthenticated and bound to the AP-only Wi-Fi
interface. There is no expectation of HTTPS. Anyone joined to the
node's AP has full control. Treat the AP credentials as the only
gate.

## See also

- User handbook: [Web UI tour](../user/web-ui.md) — narrative
  walk-through of the same endpoints from a UI perspective.
- [Mesh protocol](protocols/mesh.md) — the parallel mesh-command
  interface that drives the same operations remotely.
