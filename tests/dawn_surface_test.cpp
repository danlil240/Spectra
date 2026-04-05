// Minimal Dawn surface rendering test -- does the X11 surface present work?
// Compile: g++ -std=c++17 -o dawn_test dawn_surface_test.cpp -ldawn -lglfw -lX11
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dawn/webgpu.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>

int main()
{
    if (!glfwInit())
    {
        fprintf(stderr, "GLFW init failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* win = glfwCreateWindow(400, 300, "Dawn Surface Test", nullptr, nullptr);
    if (!win)
    {
        fprintf(stderr, "GLFW window creation failed\n");
        return 1;
    }

    // Create instance
    WGPUInstanceDescriptor inst_desc{};
    WGPUInstance           instance = wgpuCreateInstance(&inst_desc);

    // Request adapter
    struct AdapterResult
    {
        WGPUAdapter adapter = nullptr;
        bool        done    = false;
    } ar;

    WGPURequestAdapterOptions opts{};
    opts.powerPreference = WGPUPowerPreference_HighPerformance;

    WGPURequestAdapterCallbackInfo acb{};
    acb.mode = WGPUCallbackMode_AllowSpontaneous;
    acb.callback =
        [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView, void* ud1, void*)
    {
        auto* r    = (AdapterResult*)ud1;
        r->adapter = adapter;
        r->done    = true;
    };
    acb.userdata1 = &ar;
    wgpuInstanceRequestAdapter(instance, &opts, acb);
    while (!ar.done)
        wgpuInstanceProcessEvents(instance);

    if (!ar.adapter)
    {
        fprintf(stderr, "No adapter\n");
        return 1;
    }

    // Request device
    struct DeviceResult
    {
        WGPUDevice device = nullptr;
        bool       done   = false;
    } dr;

    WGPUDeviceDescriptor ddesc{};

    WGPURequestDeviceCallbackInfo dcb{};
    dcb.mode = WGPUCallbackMode_AllowSpontaneous;
    dcb.callback =
        [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView, void* ud1, void*)
    {
        auto* r   = (DeviceResult*)ud1;
        r->device = device;
        r->done   = true;
    };
    dcb.userdata1 = &dr;
    wgpuAdapterRequestDevice(ar.adapter, &ddesc, dcb);
    while (!dr.done)
        wgpuInstanceProcessEvents(instance);

    if (!dr.device)
    {
        fprintf(stderr, "No device\n");
        return 1;
    }

    WGPUQueue queue = wgpuDeviceGetQueue(dr.device);

    // Create X11 surface
    WGPUSurfaceSourceXlibWindow x11_src{};
    x11_src.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
    x11_src.display     = glfwGetX11Display();
    x11_src.window      = (uint64_t)glfwGetX11Window(win);

    WGPUSurfaceDescriptor sdesc{};
    sdesc.nextInChain   = &x11_src.chain;
    WGPUSurface surface = wgpuInstanceCreateSurface(instance, &sdesc);

    // Query capabilities
    WGPUSurfaceCapabilities caps{};
    wgpuSurfaceGetCapabilities(surface, ar.adapter, &caps);
    printf("Surface formats: %zu, preferred=%d\n",
           caps.formatCount,
           caps.formatCount > 0 ? (int)caps.formats[0] : -1);
    WGPUTextureFormat fmt = caps.formatCount > 0 ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    // Configure surface
    WGPUSurfaceConfiguration sconf{};
    sconf.device      = dr.device;
    sconf.format      = fmt;
    sconf.usage       = WGPUTextureUsage_RenderAttachment;
    sconf.width       = 400;
    sconf.height      = 300;
    sconf.presentMode = WGPUPresentMode_Fifo;
    sconf.alphaMode   = WGPUCompositeAlphaMode_Auto;
    wgpuSurfaceConfigure(surface, &sconf);

    printf("Surface configured, rendering for 5 seconds...\n");

    auto start = std::chrono::steady_clock::now();
    int  frame = 0;
    while (!glfwWindowShouldClose(win))
    {
        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - start).count();
        if (elapsed > 5.0)
            break;
        frame++;
        glfwPollEvents();

        // Acquire texture
        WGPUSurfaceTexture stex{};
        wgpuSurfaceGetCurrentTexture(surface, &stex);
        if (stex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal
            && stex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
        {
            printf("Frame %d: GetCurrentTexture failed status=%d\n", frame, (int)stex.status);
            continue;
        }
        WGPUTextureView view = wgpuTextureCreateView(stex.texture, nullptr);

        // Create encoder
        WGPUCommandEncoderDescriptor edesc{};
        WGPUCommandEncoder           enc = wgpuDeviceCreateCommandEncoder(dr.device, &edesc);

        // Render pass with bright red clear
        WGPURenderPassColorAttachment ca{};
        ca.view       = view;
        ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp     = WGPULoadOp_Clear;
        ca.storeOp    = WGPUStoreOp_Store;
        ca.clearValue = {1.0, 0.0, 0.0, 1.0};   // Bright RED

        WGPURenderPassDescriptor rpdesc{};
        rpdesc.colorAttachmentCount = 1;
        rpdesc.colorAttachments     = &ca;

        WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(enc, &rpdesc);
        wgpuRenderPassEncoderEnd(rp);
        wgpuRenderPassEncoderRelease(rp);

        // Submit
        WGPUCommandBufferDescriptor cbdesc{};
        WGPUCommandBuffer           cmds = wgpuCommandEncoderFinish(enc, &cbdesc);
        wgpuQueueSubmit(queue, 1, &cmds);
        wgpuCommandBufferRelease(cmds);
        wgpuCommandEncoderRelease(enc);

        // Present
        wgpuSurfacePresent(surface);
        wgpuDeviceTick(dr.device);

        wgpuTextureViewRelease(view);
    }

    printf("Done\n");
    wgpuSurfaceUnconfigure(surface);
    wgpuSurfaceRelease(surface);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(dr.device);
    wgpuAdapterRelease(ar.adapter);
    wgpuInstanceRelease(instance);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
