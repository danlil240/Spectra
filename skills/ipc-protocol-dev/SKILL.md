---
name: ipc-protocol-dev
description: Extend, debug, or modify the Spectra IPC binary protocol used for multi-process communication. Use when adding new message types, modifying the codec (serialize/deserialize), debugging protocol framing errors, extending the daemon session graph, or working on Python↔Backend message roundtrips. Covers the wire format, TLV payload encoding, transport layer, and daemon routing.
---

# IPC Protocol Development

Add or modify messages in the binary IPC protocol that connects Python clients, window agents, and the backend daemon.

---

## Required Context

Before starting any task, read:
- `CLAUDE.md` — architecture overview and multi-process mode
- `src/ipc/message.hpp` — message type enum, header layout, wire format
- `src/ipc/codec.hpp` / `codec.cpp` — header + payload serialization (TLV encoding)
- `src/ipc/transport.hpp` / `transport.cpp` — Unix socket send/recv (epoll-based)
- `src/daemon/` — backend daemon: session graph, figure model, client router
- `python/spectra/_codec.py` — Python-side codec (must stay in sync with C++)
- `plans/PYTHON_IPC_ARCHITECTURE.md` — protocol design decisions

---

## Protocol Wire Format

```
[Header: 40 bytes fixed] [Payload: variable, max 256 MiB]

Header layout (little-endian):
  0-1:   Magic (0x53, 0x50 = "SP")
  2-3:   MessageType (uint16_t)
  4-7:   Payload length (uint32_t)
  8-15:  Sequence number (uint64_t)
  16-23: Request ID (uint64_t)
  24-31: Session ID (uint64_t)
  32-39: Window ID (uint64_t)
```

### Payload TLV encoding

Each field in a payload: `[tag: uint8_t] [len: uint32_t LE] [data: len bytes]`

| Tag | Type | Size |
|-----|------|------|
| 0x01 | uint16_t | 2 bytes |
| 0x02 | uint32_t | 4 bytes |
| 0x03 | uint64_t | 8 bytes |
| 0x04 | string | variable (no null terminator) |
| 0x05 | float32 | 4 bytes |
| 0x06 | float64 | 8 bytes |
| 0x07 | blob | variable (raw bytes) |

---

## Workflow

### 1. Define the new message type

Add the new `MessageType` enum value in `src/ipc/message.hpp`:

```cpp
enum class MessageType : uint16_t
{
    // ... existing types ...

    // Use the next available value in the appropriate category:
    // 0x01xx = Control (Agent ↔ Backend)
    // 0x02xx = Control (Backend → Agent)
    // 0x03xx = State sync
    // 0x04xx = Events (Agent → Backend)
    // 0x05xx = Python → Backend
    // 0x054x = Backend → Python responses
    // 0x055x = Backend → Python events
    REQ_NEW_THING = 0x050B,
};
```

**Rules:**
- Never reuse a message type value — even for removed messages.
- Keep categories grouped by range.
- Use `REQ_` prefix for requests, `RESP_` for responses, `EVT_` for events, `CMD_` for commands.

### 2. Define payload fields

Document the payload structure as a comment next to the message type or in the encoder/decoder:

```cpp
// REQ_NEW_THING payload:
//   tag 0x03: figure_id (uint64_t)
//   tag 0x04: label (string)
//   tag 0x05: value (float32)
```

### 3. Add C++ codec functions

In `src/ipc/codec.cpp`, add encode/decode functions:

```cpp
std::vector<uint8_t> encode_new_thing(uint64_t figure_id,
                                       const std::string& label,
                                       float value)
{
    PayloadEncoder enc;
    enc.put_u64(0x03, figure_id);
    enc.put_string(0x04, label);
    enc.put_f32(0x05, value);

    Message msg;
    msg.header.type = MessageType::REQ_NEW_THING;
    msg.payload = enc.take();
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return encode_message(msg);
}
```

### 4. Add Python codec mirror

In `python/spectra/_codec.py`, add the matching encode/decode:

```python
def encode_new_thing(figure_id: int, label: str, value: float) -> bytes:
    payload = b""
    payload += _encode_u64(0x03, figure_id)
    payload += _encode_string(0x04, label)
    payload += _encode_f32(0x05, value)
    return _encode_header(MessageType.REQ_NEW_THING, payload) + payload
```

**Critical:** The Python codec must stay byte-identical to the C++ codec. Use the same TLV tags, field order, and encoding rules.

### 5. Handle the message in the daemon

In `src/daemon/`, add routing for the new message:

- **`client_router.hpp`** — route the message to the right handler
- **`session_graph.cpp`** — if the message affects the session model
- **`figure_model.cpp`** — if the message modifies figure/axes/series state

### 6. Add cross-language roundtrip test

In `tests/unit/test_cross_codec.cpp`, add a test that encodes in C++ and decodes in Python (and vice versa):

```cpp
TEST(CrossCodec, NewThingRoundtrip)
{
    auto encoded = encode_new_thing(42, "test", 3.14f);
    auto decoded = decode_message(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->header.type, MessageType::REQ_NEW_THING);
    // Verify payload fields...
}
```

Also add the Python-side test in `python/tests/test_cross_codec.py`.

### 7. Build and validate

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run IPC-specific tests
ctest --test-dir build -R "ipc|codec|cross" --output-on-failure

# Run all non-GPU tests
ctest --test-dir build -LE gpu --output-on-failure
```

---

## Message Type Registry

| Range | Category | Direction |
|-------|----------|-----------|
| 0x0001-0x000F | Handshake | Bidirectional |
| 0x0010-0x001F | Request/Response | Bidirectional |
| 0x0100-0x01FF | Control | Agent → Backend |
| 0x0200-0x02FF | Control | Backend → Agent |
| 0x0300-0x03FF | State sync | Bidirectional |
| 0x0400-0x04FF | Events | Agent → Backend |
| 0x0500-0x052F | Python lifecycle | Python → Backend |
| 0x0530-0x053F | Python session | Python → Backend |
| 0x0540-0x054F | Python responses | Backend → Python |
| 0x0550-0x055F | Python events | Backend → Python |

---

## Issue-to-File Map

| Issue type | Primary file(s) |
|---|---|
| New message type definition | `src/ipc/message.hpp` |
| C++ codec (serialize/deserialize) | `src/ipc/codec.cpp`, `codec.hpp` |
| Python codec | `python/spectra/_codec.py` |
| Wire format / header issues | `src/ipc/message.hpp`, `src/ipc/codec.cpp` |
| Transport (socket send/recv) | `src/ipc/transport.cpp`, `transport.hpp` |
| Large payload handling | `src/ipc/blob_store.hpp` |
| Daemon message routing | `src/daemon/client_router.hpp` |
| Session graph mutations | `src/daemon/session_graph.cpp` |
| Figure model mutations | `src/daemon/figure_model.cpp` |
| Process management (agent spawn) | `src/daemon/process_manager.cpp` |
| Agent-side message handling | `src/agent/main.cpp` |
| Cross-language codec tests | `tests/unit/test_cross_codec.cpp`, `python/tests/test_cross_codec.py` |
| IPC unit tests | `tests/unit/test_ipc.cpp` |
| Python IPC tests | `tests/unit/test_python_ipc.cpp` |
| Protocol version negotiation | `HELLO`/`WELCOME` handshake in `message.hpp` |

---

## Protocol Versioning

- Current version: **v1.0** (`PROTOCOL_MAJOR=1`, `PROTOCOL_MINOR=0`)
- Backwards-compatible additions (new message types, new optional TLV fields) bump MINOR.
- Breaking changes (header layout, removed fields, changed semantics) bump MAJOR.
- The `HELLO`/`WELCOME` handshake includes version negotiation.
- Never remove or reuse a `MessageType` enum value — mark as deprecated.

---

## Common Pitfalls

1. **C++ and Python codec out of sync** — Every encode/decode change must be mirrored in both languages. Run `test_cross_codec` to catch mismatches.
2. **Wrong TLV tag** — Using the same tag for two different fields in one payload causes silent data corruption.
3. **Endianness** — All multi-byte values are little-endian on the wire. The codec handles this but manual byte manipulation must follow the convention.
4. **Payload size exceeds 256 MiB** — The protocol rejects payloads larger than `MAX_PAYLOAD_SIZE`. Use the blob store for very large data.
5. **Missing request correlation** — Requests and responses are correlated by `request_id`. Always set it on outgoing requests and echo it on responses.
6. **Forgetting sequence numbers** — Each side maintains a monotonic sequence counter. Out-of-order detection relies on this.

---

## Live Smoke Test via MCP Server

After building, verify the IPC round-trip works by sending a command through the MCP server (which internally routes through the automation server):

```bash
pkill -f spectra || true; sleep 0.5
./build/app/spectra &
sleep 1

# Health check
curl http://127.0.0.1:8765/

# get_state exercises IPC from the automation layer
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_state","arguments":{}}}'

# create_figure + add_series exercises full IPC command path
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"create_figure","arguments":{"width":800,"height":600}}}'
```

MCP env vars: `SPECTRA_MCP_PORT` (default `8765`), `SPECTRA_MCP_BIND` (default `127.0.0.1`).

---

## Guardrails

- Never reuse a `MessageType` enum value.
- Never change the 40-byte header layout without bumping `PROTOCOL_MAJOR`.
- Always update both C++ and Python codecs in the same change.
- Always add a cross-language roundtrip test for new message types.
- Never skip the `HELLO`/`WELCOME` handshake in new client implementations.
- Preserve explicit IPC versioning, ordering, and request correlation.
- Keep payloads under the 256 MiB limit; use blob store for large data.
