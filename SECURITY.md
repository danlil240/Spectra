# Security Policy

## Supported versions

Spectra is pre-1.0. Security fixes land on `main` and ship in the next release; only the
latest `0.x` release is supported.

| Version          | Supported |
|------------------|-----------|
| latest `0.x`     | Yes       |
| older `0.x`      | No — please upgrade |

## Reporting a vulnerability

Please **do not** open a public issue, pull request or discussion for security problems.

Report privately through GitHub's private vulnerability reporting:

1. Go to <https://github.com/danlil240/Spectra/security/advisories/new>
   (repository **Security** tab → **Report a vulnerability**).
2. Describe the issue, the affected version/commit, and steps to reproduce
   (a minimal proof of concept helps).

You should get an acknowledgement within a few days. We will work with you on a fix and a
coordinated disclosure date, and credit you in the advisory unless you prefer otherwise.

## Automation and MCP server

Spectra includes an automation interface used by QA tooling and AI agents:

- a JSON-lines **Unix socket** (`spectra-auto-<pid>.sock`), and
- an **MCP HTTP server** (JSON-RPC, `http://127.0.0.1:8765/mcp` by default; it tries
  ports 8765–8780 when the default is taken).

Through these endpoints a client can run any UI command, inject keyboard and mouse input,
create figures and write screenshots. Treat access to them as equivalent to control of
the running application.

### Off by default

Neither endpoint starts unless you turn it on explicitly, in one of these ways:

| How | Effect |
|-----|--------|
| `AppConfig{.enable_automation = true}` | Programmatic opt-in for an embedding app |
| `SPECTRA_AUTOMATION=1` | Environment opt-in (`1`, `true`, `yes`, `on`) |
| `SPECTRA_AUTO_SOCKET=/path/to.sock` | Opt-in with an explicit socket path |

```bash
export SPECTRA_AUTOMATION=1
export SPECTRA_MCP_TOKEN="$(openssl rand -hex 32)"   # optional; otherwise one is generated
./build/spectra
```

### Unix socket

- Created under `$XDG_RUNTIME_DIR` (a per-user `0700` directory) when it is set, otherwise
  `$TMPDIR` or `/tmp`.
- The socket node is created with `umask 077`, so only the owner can connect, and the
  server also checks the peer's uid (`SO_PEERCRED` / `getpeereid`) and drops connections
  from other users.

### MCP HTTP server

- Binds to `127.0.0.1` only. `SPECTRA_MCP_BIND` can change this, and Spectra logs a warning
  when it does. Do not expose the server on a network you do not trust.
- **Token auth.** Every request, including `GET /`, must carry
  `Authorization: Bearer <token>` (or `X-Spectra-Token: <token>`). The token is either
  `$SPECTRA_MCP_TOKEN`, or a random 256-bit value generated for each process.
- **Token discovery.** On startup the server writes
  `<runtime dir>/spectra-mcp-<pid>.json` with mode `0600`, containing
  `{"endpoint": ..., "token": ..., "pid": ...}`, and logs the file path (never the token).
  The file is deleted when the server stops.
- **Browser protections.**
  - Requests that carry an `Origin` header are rejected, which blocks cross-site requests
    from web pages.
  - The `Host` header must be `127.0.0.1`, `localhost` or `[::1]`, optionally with the
    server's port. This blocks DNS rebinding.
  - `POST` requests must use `Content-Type: application/json`, so a `text/plain` or form
    request a browser can send without a preflight is refused.

Example client call:

```bash
TOKEN_FILE=$(ls -t "${XDG_RUNTIME_DIR:-/tmp}"/spectra-mcp-*.json | head -1)
TOKEN=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["token"])' "$TOKEN_FILE")
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}'
```

### Screenshot output paths

`capture_screenshot` and `capture_window` write only inside the capture directory:

- `$SPECTRA_CAPTURE_DIR` if it is set, otherwise
- `$XDG_RUNTIME_DIR/spectra-captures`, otherwise
- `<tmp>/spectra-captures-<uid>`.

The directory is created with mode `0700` and must be a real directory (not a symlink)
owned by the current user. A `path` argument is resolved relative to that directory.
Absolute paths outside it, and any `..` component, are rejected. The tool result returns
the absolute path that was written.
