# Debug Vulkan Issues

Diagnose and fix Vulkan rendering problems.

## Common Issues

### Validation Errors
- Enable Vulkan validation layers: build with Debug and set `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`
- Check `vk_backend.cpp` for instance/device creation
- Verify required extensions are enabled

### Swapchain / Present Issues
- Check `vk_swapchain.cpp` for format/present mode selection
- Verify window resize handling (swapchain recreation)
- Check framebuffer attachment compatibility

### Buffer Upload Issues
- Check `vk_buffer.cpp` for VMA allocation flags
- Verify staging buffer -> device buffer copy barriers
- Check buffer size calculations match vertex/index counts

### Pipeline Issues
- Verify SPIR-V compilation in `cmake/CompileShaders.cmake`
- Check push constant ranges in `vk_pipeline.cpp`
- Verify vertex input bindings match shader expectations

## Diagnostic Commands

```bash
# Check Vulkan support
vulkaninfo --summary

# List available ICDs
ls /usr/share/vulkan/icd.d/

# Run with validation layers
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./build/examples/basic_line

# Run with software renderer (lavapipe)
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json ./build/examples/basic_line
```

## Live State Inspection via MCP Server

While Spectra is running, use the MCP server to inspect live state and capture screenshots for debugging:

```bash
# Kill any stale instance, start fresh
pkill -f spectra || true; sleep 0.5
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./build/app/spectra &
sleep 1

# Health check
curl http://127.0.0.1:8765/

# Get application state (active figure, window info)
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_state","arguments":{}}}'

# Capture screenshot for visual inspection
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"capture_window","arguments":{"path":"/tmp/debug_snap.png"}}}'
```

See `.windsurf/skills/spectra-skills/SKILL.md` § 12 for the full tool reference (22 tools).
