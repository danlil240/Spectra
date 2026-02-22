# Python Bindings

Work with the Python IPC bindings for Spectra.

## Architecture

Python communicates with the Spectra backend daemon via Unix sockets using a binary IPC protocol. The Python package mirrors the C++ API.

## Key Files

- `python/spectra/_easy.py` - Easy API (MATLAB-style: `spectra.plot()`, `spectra.show()`)
- `python/spectra/_session.py` - IPC session management
- `python/spectra/_codec.py` - Message serialization (must match `src/ipc/codec.cpp`)
- `python/spectra/_figure.py` - Figure wrapper
- `python/spectra/_axes.py` - Axes wrapper
- `python/spectra/_series.py` - Series wrapper
- `python/spectra/_transport.py` - Socket I/O

## Adding New IPC Messages

1. Define the message type in `src/ipc/message.hpp`
2. Add serialization in `src/ipc/codec.cpp`
3. Add deserialization in `src/ipc/codec.cpp`
4. Mirror the codec in `python/spectra/_codec.py`
5. Add handler in `src/daemon/main.cpp`
6. Add cross-codec test in `tests/unit/test_cross_codec.cpp`

## Important

- The C++ codec and Python codec must stay in sync
- Test with `tests/unit/test_ipc.cpp` and `python/tests/`
- Binary protocol is little-endian
