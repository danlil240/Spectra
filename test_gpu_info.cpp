#include <iostream>

int main() {
    std::cout << "=== GPU Diagnostics ===" << std::endl;
    
    // Check NVIDIA driver version
    std::system("nvidia-smi --query-gpu=driver_version,name,temperature.power.limit --format=csv,noheader,nounits 2>/dev/null || echo 'nvidia-smi not available'");
    
    std::cout << "\n=== GPU Memory Usage ===" << std::endl;
    std::system("nvidia-smi --query-gpu=memory.used,memory.total --format=csv,noheader,nounits 2>/dev/null || echo 'Cannot query memory usage'");
    
    std::cout << "\n=== System Info ===" << std::endl;
    std::system("uname -r");
    std::system("cat /proc/version 2>/dev/null | head -1");
    
    std::cout << "\n=== Vulkan Devices ===" << std::endl;
    std::system("vulkaninfo --summary 2>/dev/null | head -20 || echo 'vulkaninfo not available'");
    
    std::cout << "\n=== Dmesg GPU Errors ===" << std::endl;
    std::system("dmesg | grep -i 'nvidia\\|gpu\\|drm' | tail -10 || echo 'No GPU messages in dmesg'");
    
    return 0;
}
