// ─── Easy Embed Demo (C++) ─────────────────────────────────────────────────────
//
// Demonstrates the spectra::easy_embed API for one-liner offscreen rendering.
// No windows, no event loop, no daemon — just data in → pixels out.
//
// Build:
//   cmake -DSPECTRA_BUILD_EMBED_SHARED=ON ..
//   g++ -std=c++20 -I include examples/easy_embed_demo.cpp -L build -lspectra -o easy_embed_demo
//
// Usage:
//   ./easy_embed_demo
//

#include <spectra/spectra.hpp>
#include <iostream>
#include <vector>
#include <cmath>

void demo_basic_line()
{
    std::cout << "📈 Basic line plot\n";
    std::vector<float> x = {0, 1, 2, 3, 4, 5};
    std::vector<float> y = {0, 1, 4, 9, 16, 25};  // y = x^2
    
    auto img = spectra::render(x, y);
    std::cout << "   Rendered " << img.width << "x" << img.height 
              << " image (" << img.size_bytes() << " bytes)\n";
    
    // Count non-zero pixels to verify something was rendered
    size_t nonzero = 0;
    for (uint8_t b : img.data)
        if (b != 0) ++nonzero;
    std::cout << "   Non-zero pixels: " << nonzero << "\n\n";
}

void demo_save_to_file()
{
    std::cout << "💾 Save to PNG file\n";
    std::vector<float> x = {0, 1, 2, 3, 4, 5};
    std::vector<float> y = {0, 1, 4, 9, 16, 25};
    
    spectra::RenderOptions opts;
    opts.save_path = "easy_embed_demo.png";
    auto img = spectra::render(x, y, opts);
    
    std::cout << "   Saved to " << opts.save_path << "\n\n";
}

void demo_custom_size()
{
    std::cout << "🖼️  Custom size and styling\n";
    std::vector<float> x = {0, 1, 2, 3, 4, 5};
    std::vector<float> y = {0, 1, 4, 9, 16, 25};
    
    spectra::RenderOptions opts;
    opts.width = 1920;
    opts.height = 1080;
    opts.fmt = "r--o";  // Red dashed line with circles
    
    auto img = spectra::render(x, y, opts);
    std::cout << "   HD render: " << img.width << "x" << img.height 
              << " with red dashed line and circles\n\n";
}

void demo_scatter()
{
    std::cout << "🔵 Scatter plot\n";
    std::vector<float> x, y;
    
    // Generate random scatter data
    std::srand(42);
    for (int i = 0; i < 100; ++i)
    {
        x.push_back(std::rand() / float(RAND_MAX) * 10.0f);
        y.push_back(std::rand() / float(RAND_MAX) * 10.0f);
    }
    
    spectra::RenderOptions opts;
    opts.save_path = "scatter_demo.png";
    auto img = spectra::render_scatter(x, y, opts);
    std::cout << "   Scatter plot with 100 points saved to scatter_demo.png\n\n";
}

void demo_multi_series()
{
    std::cout << "📊 Multi-series plot\n";
    std::vector<float> x;
    for (int i = 0; i < 100; ++i)
        x.push_back(i * 0.1f);
    
    std::vector<float> y1, y2, y3;
    for (float v : x)
    {
        y1.push_back(std::sin(v));
        y2.push_back(std::cos(v));
        y3.push_back(v * 0.5f);  // Linear
    }
    
    spectra::RenderOptions opts;
    opts.title = "Trigonometric Functions";
    opts.save_path = "multi_demo.png";
    
    auto img = spectra::render_multi({
        {x, y1, "b-", "sin(x)"},
        {x, y2, "r--", "cos(x)"},
        {x, y3, "g:", "0.5x"},
    }, opts);
    
    std::cout << "   Multi-series plot with sin, cos, and linear functions\n";
    std::cout << "   Saved to multi_demo.png\n\n";
}

void demo_histogram()
{
    std::cout << "📊 Histogram\n";
    std::vector<float> data;
    
    // Generate normal distribution data
    std::srand(123);
    for (int i = 0; i < 1000; ++i)
    {
        // Box-Muller transform for normal distribution
        float u1 = std::rand() / float(RAND_MAX);
        float u2 = std::rand() / float(RAND_MAX);
        float z0 = std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * M_PI * u2);
        data.push_back(z0);
    }
    
    spectra::RenderOptions opts;
    opts.save_path = "histogram_demo.png";
    auto img = spectra::render_histogram(data, 30, opts);
    std::cout << "   Histogram of 1000 normal samples (30 bins)\n";
    std::cout << "   Saved to histogram_demo.png\n\n";
}

void demo_with_labels()
{
    std::cout << "📝 Plot with labels\n";
    std::vector<float> x = {0, 1, 2, 3, 4, 5};
    std::vector<float> y = {0, 1, 4, 9, 16, 25};
    
    spectra::RenderOptions opts;
    opts.title = "Quadratic Growth";
    opts.xlabel = "Time (seconds)";
    opts.ylabel = "Value";
    opts.save_path = "labeled_demo.png";
    
    auto img = spectra::render(x, y, opts);
    std::cout << "   Plot with title and axis labels\n";
    std::cout << "   Saved to labeled_demo.png\n\n";
}

void demo_performance()
{
    std::cout << "⚡ Performance test\n";
    
    // Large dataset
    const size_t n = 50000;
    std::vector<float> x, y;
    x.reserve(n);
    y.reserve(n);
    
    for (size_t i = 0; i < n; ++i)
    {
        x.push_back(i * 0.01f);
        y.push_back(std::sin(x[i] * 0.5f) + (std::rand() / float(RAND_MAX) - 0.5f) * 0.2f);
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    spectra::RenderOptions opts;
    opts.width = 1600;
    opts.height = 900;
    auto img = spectra::render(x, y, opts);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto elapsed = std::chrono::duration<float>(end - start).count();
    std::cout << "   Rendered " << n << " points in " << elapsed << " seconds\n";
    std::cout << "   Image size: " << img.width << "x" << img.height 
              << " (" << img.size_bytes() << " bytes)\n";
    std::cout << "   Throughput: " << size_t(n / elapsed) << " points/second\n\n";
}

void demo_rendered_image()
{
    std::cout << "🖼️  RenderedImage object API\n";
    std::vector<float> x = {0, 1, 2, 3, 4};
    std::vector<float> y = {0, 1, 4, 9, 16};
    
    auto img = spectra::render(x, y);
    
    std::cout << "   RenderedImage attributes:\n";
    std::cout << "     .width = " << img.width << "\n";
    std::cout << "     .height = " << img.height << "\n";
    std::cout << "     .stride() = " << img.stride() << "\n";
    std::cout << "     .size_bytes() = " << img.size_bytes() << "\n";
    std::cout << "     .empty() = " << (img.empty() ? "true" : "false") << "\n";
    std::cout << "     .pixels() returns " << (void*)img.pixels() << "\n\n";
}

int main()
{
    std::cout << "🚀 Spectra Easy Embed Demo (C++)\n";
    std::cout << "=====================================\n\n";
    
    try
    {
        // Run all demos
        demo_basic_line();
        demo_save_to_file();
        demo_custom_size();
        demo_scatter();
        demo_multi_series();
        demo_histogram();
        demo_with_labels();
        demo_performance();
        demo_rendered_image();
        
        std::cout << "✅ All demos completed successfully!\n\n";
        
        // List generated files
        std::cout << "Generated files:\n";
        const char* files[] = {
            "easy_embed_demo.png",
            "scatter_demo.png", 
            "multi_demo.png",
            "histogram_demo.png",
            "labeled_demo.png"
        };
        
        for (const char* file : files)
        {
            std::ifstream f(file);
            if (f.good())
            {
                f.seekg(0, std::ios::end);
                auto size = f.tellg();
                std::cout << "   " << file << " (" << size << " bytes)\n";
            }
        }
        
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "❌ Error: " << e.what() << "\n";
        std::cerr << "\nMake sure you built with Vulkan support:\n";
        std::cerr << "  cmake -DSPECTRA_BUILD_EMBED_SHARED=ON ..\n";
        return 1;
    }
}
