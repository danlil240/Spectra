---
name: python-bindings
description: Develop, extend, or debug the Spectra Python API and bindings. Use when adding new Python plotting functions, extending the easy API, fixing Python↔Backend communication, debugging session/transport issues, adding Python examples, or updating the Python package. Covers the Python client library, IPC codec mirror, session management, embedding API, and Qt backend integration.
---

# Python Bindings Development

Extend or debug the Spectra Python client library that communicates with the backend daemon over IPC.

---

## Required Context

Before starting any task, read:
- `CLAUDE.md` — project overview and Python architecture
- `python/spectra/` — Python package source
- `python/spectra/_codec.py` — Python IPC codec (must mirror C++ codec)
- `python/spectra/_session.py` — session lifecycle management
- `python/spectra/_transport.py` — Unix socket transport
- `python/spectra/_easy.py` — MATLAB-style easy API (`plot`, `scatter`, `show`)
- `python/spectra/_figure.py` — Figure class (object-oriented API)
- `python/tests/` — Python test suite
- `plans/PYTHON_IPC_ARCHITECTURE.md` — design decisions
- `src/ipc/message.hpp` — C++ message types (must stay in sync)

---

## Python Package Structure

```
python/spectra/
├── __init__.py            # Package entry, re-exports public API
├── _easy.py               # MATLAB-style API: plot(), scatter(), title(), show()
├── _figure.py             # Figure class (OOP API)
├── _axes.py               # Axes class
├── _series.py             # Series class
├── _animation.py          # Animation builders
├── _blob.py               # Large data blob handling
├── _cli.py                # CLI entry point
├── _codec.py              # IPC codec (mirrors C++ codec exactly)
├── _embed.py              # Embedding API for GUI frameworks
├── _errors.py             # Exception hierarchy
├── _launcher.py           # Backend daemon launcher
├── _log.py                # Logging configuration
├── _persistence.py        # Session save/restore
├── _protocol.py           # Protocol constants and message types
├── _session.py            # Session lifecycle (connect, heartbeat, reconnect)
├── _transport.py          # Unix socket transport layer
├── embed.py               # Public embedding entry point
├── backends/              # Platform-specific backend management
python/examples/           # 16 demo scripts
python/tests/              # 7 test files
```

---

## Workflow

### 1. Adding a new Python API function

**Easy API** (MATLAB-style, module-level functions in `_easy.py`):
```python
def new_func(data, **kwargs):
    """One-line description."""
    session = _get_or_create_session()
    fig = session.current_figure()
    ax = fig.current_axes()
    series = ax.add_series("new_type", data, **kwargs)
    return series
```

**OOP API** (methods on Figure/Axes/Series classes):
```python
class Axes:
    def new_method(self, data, **kwargs):
        msg = codec.encode_new_thing(self._id, data, **kwargs)
        self._session.send(msg)
        resp = self._session.recv()
        return self._handle_response(resp)
```

### 2. Adding a new message type (Python side)

When a new C++ `MessageType` is added (see `skills/ipc-protocol-dev/SKILL.md`), mirror it in Python:

1. Add the enum value in `_protocol.py`:
   ```python
   class MessageType(IntEnum):
       REQ_NEW_THING = 0x050B
   ```

2. Add encode/decode in `_codec.py`:
   ```python
   def encode_new_thing(figure_id: int, label: str, value: float) -> bytes:
       payload = b""
       payload += _encode_u64(0x03, figure_id)
       payload += _encode_string(0x04, label)
       payload += _encode_f32(0x05, value)
       return _encode_header(MessageType.REQ_NEW_THING, payload) + payload
   ```

3. Add a cross-codec roundtrip test in `python/tests/test_cross_codec.py`.

### 3. Adding Python examples

Create a new file in `python/examples/`:

```python
#!/usr/bin/env python3
"""Short description of what the example demonstrates."""

import spectra

# Use the easy API
spectra.plot([1, 2, 3, 4], [1, 4, 9, 16], "r-o", label="squares")
spectra.title("Example Title")
spectra.show()
```

### 4. Adding Python tests

Use `pytest` in `python/tests/`:

```python
def test_new_feature():
    """Test description."""
    import spectra
    fig = spectra.figure()
    ax = fig.add_axes()
    series = ax.plot([1, 2, 3], [4, 5, 6])
    assert series is not None
```

### 5. Build and validate

```bash
# Build the C++ backend (required for IPC tests)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run C++ IPC/codec tests
ctest --test-dir build -R "ipc|codec|python" --output-on-failure

# Run Python tests
cd python && python -m pytest tests/ -v

# Run cross-codec tests (requires both C++ and Python)
ctest --test-dir build -R cross_codec --output-on-failure
```

---

## API Layers

| Layer | Module | Pattern | Example |
|-------|--------|---------|---------|
| Easy API | `_easy.py` | Module functions, implicit session | `spectra.plot(x, y, "r-o")` |
| OOP API | `_figure.py`, `_axes.py`, `_series.py` | Object methods, explicit lifecycle | `fig.axes[0].plot(x, y)` |
| Embedding | `_embed.py`, `embed.py` | Qt/GUI framework integration | `widget = spectra.embed(parent)` |
| Animation | `_animation.py` | Builder pattern | `spectra.animate(fig, duration=2.0)` |
| Session | `_session.py` | Connection management | (internal, not public API) |

---

## Issue-to-File Map

| Issue type | Primary file(s) |
|---|---|
| Easy API functions (plot, scatter, etc.) | `python/spectra/_easy.py` |
| Figure/Axes/Series OOP API | `python/spectra/_figure.py`, `_axes.py`, `_series.py` |
| IPC codec / message encoding | `python/spectra/_codec.py` |
| Protocol constants | `python/spectra/_protocol.py` |
| Session connect/disconnect/heartbeat | `python/spectra/_session.py` |
| Transport (socket I/O) | `python/spectra/_transport.py` |
| Backend daemon launcher | `python/spectra/_launcher.py` |
| Embedding in Qt/other frameworks | `python/spectra/_embed.py`, `python/spectra/embed.py` |
| Animation API | `python/spectra/_animation.py` |
| Large data blobs | `python/spectra/_blob.py` |
| Error handling / exceptions | `python/spectra/_errors.py` |
| Package metadata / setup | `pyproject.toml` |
| Python test failures | `python/tests/` |
| Cross-codec sync issues | `python/spectra/_codec.py` ↔ `src/ipc/codec.cpp` |

---

## Format String Syntax (MATLAB-style)

The easy API supports format strings like MATLAB: `"r--o"` = red dashed line with circle markers.

| Component | Options |
|-----------|---------|
| Color | `r` red, `g` green, `b` blue, `k` black, `w` white, `c` cyan, `m` magenta, `y` yellow |
| Line style | `-` solid, `--` dashed, `:` dotted, `-.` dash-dot, (none) = no line |
| Marker | `o` circle, `s` square, `^` triangle, `d` diamond, `+` plus, `x` cross, `.` point |

---

## Common Pitfalls

1. **Codec mismatch** — The `_codec.py` encode/decode must be byte-identical to C++ `codec.cpp`. Always run cross-codec tests after changes.
2. **Implicit session** — The easy API creates a session lazily. If the backend daemon isn't running, `show()` will attempt to launch it.
3. **Blocking recv** — `_transport.py` uses blocking reads. A missing response from the daemon will hang the Python process. Use heartbeat timeouts.
4. **Message type enum out of sync** — `_protocol.py` must mirror `src/ipc/message.hpp`. Adding a type in C++ without Python causes `Unknown message type` errors.
5. **NumPy array conversion** — Data arrays should be converted to `float32` contiguous before encoding to avoid serialization overhead.

---

## Live Smoke Test via MCP Server

After building and installing the Python package, verify the full stack — Python → IPC → Spectra — using the MCP server as an independent signal:

```bash
pkill -f spectra || true; sleep 0.5
./build/app/spectra &
sleep 1

# Verify Spectra is up
curl http://127.0.0.1:8765/

# Check state (confirms app is responsive to automation)
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_state","arguments":{}}}'

# After running a Python example, confirm via MCP that figures were created
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"get_figure_info","arguments":{"figure_id":1}}}'
```

MCP env vars: `SPECTRA_MCP_PORT` (default `8765`), `SPECTRA_MCP_BIND` (default `127.0.0.1`).

---

## Guardrails

- Never change `_codec.py` without updating `src/ipc/codec.cpp` (and vice versa).
- Never break the easy API by changing function signatures — use `**kwargs` for new optional parameters.
- Always run cross-codec roundtrip tests after any codec change.
- Always pin protocol version constants to match C++ values.
- Follow `pyproject.toml` metadata for versioning — keep in sync with `version.txt`.
- Don't add heavyweight dependencies to the Python package — keep it lightweight.
