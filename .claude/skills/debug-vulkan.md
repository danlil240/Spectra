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
