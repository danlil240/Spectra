#!/usr/bin/env python3
"""
Easy Embed Demo — One-liner offscreen rendering with Spectra.

Shows the new spectra.embed easy API for rendering plots to pixels or files
without any windows, daemon, or event loop. Perfect for:
- Web services (plot as PNG endpoint)
- Data pipelines (batch report generation)
- Scientific notebooks (embed plots in documents)
- Game engines (render UI overlays)

Build requirements:
    cmake -DSPECTRA_BUILD_EMBED_SHARED=ON ..
    export SPECTRA_EMBED_LIB=/path/to/build/libspectra_embed.so
"""

import math
import os
import sys
import tempfile
from pathlib import Path

# Ensure we can import spectra from the source tree
sys.path.insert(0, str(Path(__file__).parent.parent))

import spectra.embed as spe


def demo_basic_line():
    """Simple line plot to pixels."""
    print("📈 Basic line plot")
    x = [0, 1, 2, 3, 4, 5]
    y = [0, 1, 4, 9, 16, 25]  # y = x^2
    
    img = spe.render(x, y)
    print(f"   Rendered {img.width}x{img.height} image ({len(img)} bytes)")
    print(f"   Non-zero pixels: {sum(1 for b in img.data if b != 0)}")
    print()


def demo_save_to_file():
    """Render and save to PNG file."""
    print("💾 Save to PNG file")
    x = [0, 1, 2, 3, 4, 5]
    y = [0, 1, 4, 9, 16, 25]
    
    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
        path = f.name
    
    spe.render(x, y, save=path)
    size = os.path.getsize(path)
    print(f"   Saved to {path} ({size} bytes)")
    
    # Clean up
    os.unlink(path)
    print()


def demo_custom_size():
    """Custom resolution."""
    print("🖼️  Custom size")
    x = [0, 1, 2, 3, 4, 5]
    y = [0, 1, 4, 9, 16, 25]
    
    img = spe.render(x, y, width=1920, height=1080)
    print(f"   HD render: {img.width}x{img.height}")
    print()


def demo_scatter():
    """Scatter plot."""
    print("🔵 Scatter plot")
    import random
    
    # Generate random scatter data
    random.seed(42)
    x = [random.random() * 10 for _ in range(100)]
    y = [random.random() * 10 for _ in range(100)]
    
    spe.scatter(x, y, save="scatter_demo.png")
    print("   Scatter plot with 100 points saved to scatter_demo.png")
    print()


def demo_multi_series():
    """Multiple series on one plot."""
    print("📊 Multi-series plot")
    import math
    
    x = [i * 0.1 for i in range(100)]
    y1 = [math.sin(v) for v in x]
    y2 = [math.cos(v) for v in x]
    y3 = [v * 0.5 for v in x]  # Linear
    
    spe.render_multi([
        (x, y1, "b-", "sin(x)"),
        (x, y2, "r--", "cos(x)"),
        (x, y3, "g:", "0.5x"),
    ], title="Trigonometric Functions", save="multi_demo.png")
    
    print("   Multi-series plot with sin, cos, and linear functions")
    print("   Saved to multi_demo.png")
    print()


def demo_histogram():
    """Histogram plot."""
    print("📊 Histogram")
    import random
    
    # Generate normal distribution data
    random.seed(123)
    data = [random.gauss(0, 1) for _ in range(1000)]
    
    spe.histogram(data, bins=30, save="histogram_demo.png")
    print("   Histogram of 1000 normal samples (30 bins)")
    print("   Saved to histogram_demo.png")
    print()


def demo_with_labels():
    """Plot with title and axis labels."""
    print("📝 Plot with labels")
    x = [0, 1, 2, 3, 4, 5]
    y = [0, 1, 4, 9, 16, 25]
    
    spe.render(x, y,
               title="Quadratic Growth",
               xlabel="Time (seconds)",
               ylabel="Value",
               save="labeled_demo.png")
    
    print("   Plot with title and axis labels")
    print("   Saved to labeled_demo.png")
    print()


def demo_numpy_support():
    """Show numpy array support."""
    print("🔢 NumPy array support")
    try:
        import numpy as np
    except ImportError:
        print("   NumPy not available, skipping demo")
        return
    
    # Create numpy arrays
    x = np.linspace(0, 4 * np.pi, 200)
    y = np.sin(x) * np.exp(-x / (4 * np.pi))  # Damped sine wave
    
    spe.render(x, y, save="numpy_demo.png")
    print("   Rendered damped sine wave from numpy arrays")
    print(f"   Array shapes: x={x.shape}, y={y.shape}")
    print("   Saved to numpy_demo.png")
    print()


def demo_performance():
    """Performance test with large dataset."""
    print("⚡ Performance test")
    import time
    import numpy as np
    
    # Large dataset
    n = 50000
    x = np.linspace(0, 100, n)
    y = np.sin(x * 0.5) + np.random.normal(0, 0.1, n)
    
    start = time.time()
    img = spe.render(x, y, width=1600, height=900)
    elapsed = time.time() - start
    
    print(f"   Rendered {n:,} points in {elapsed:.3f} seconds")
    print(f"   Image size: {img.width}x{img.height} ({len(img):,} bytes)")
    print(f"   Throughput: {n/elapsed:,.0f} points/second")
    print()


def demo_batch_processing():
    """Batch processing multiple plots."""
    print("📦 Batch processing")
    
    plots = [
        ("linear", lambda i: (list(range(10)), list(range(10)))),
        ("quadratic", lambda i: (list(range(10)), [j*j for j in range(10)])),
        ("cubic", lambda i: (list(range(10)), [j*j*j for j in range(10)])),
        ("sine", lambda i: (list(range(10)), [math.sin(j) for j in range(10)])),
    ]
    
    with tempfile.TemporaryDirectory() as tmpdir:
        for name, func in plots:
            x, y = func(0)
            path = os.path.join(tmpdir, f"{name}.png")
            spe.render(x, y, save=path)
            print(f"   Generated {name}.png")
        
        print(f"   All plots saved to {tmpdir}")
    print()


def demo_image_object():
    """Show Image object methods."""
    print("🖼️  Image object API")
    x = [0, 1, 2, 3, 4]
    y = [0, 1, 4, 9, 16]
    
    img = spe.render(x, y)
    
    print(f"   Image attributes:")
    print(f"     .width = {img.width}")
    print(f"     .height = {img.height}")
    print(f"     .stride = {img.stride}")
    print(f"     .size_bytes = {img.size_bytes}")
    print(f"     len(img) = {len(img)}")
    print(f"     bool(img) = {bool(img)}")
    print(f"     img.data is {len(img.data)} bytes of type {type(img.data)}")
    print()


def main():
    """Run all demos."""
    print("🚀 Spectra Easy Embed Demo")
    print("=" * 50)
    print()
    
    # Check if embed library is available
    try:
        # Try a minimal render to verify the library works
        spe.render([0, 1], [0, 1])
    except Exception as e:
        print(f" Error: {e}")
        print()
        print("Make sure you built the embed shared library:")
        print("  cmake -DSPECTRA_BUILD_EMBED_SHARED=ON ..")
        print("  export SPECTRA_EMBED_LIB=/path/to/build/libspectra_embed.so")
        print()
        return 1
    
    # Run demos
    demo_basic_line()
    demo_save_to_file()
    demo_custom_size()
    demo_scatter()
    demo_multi_series()
    demo_histogram()
    demo_with_labels()
    demo_numpy_support()
    demo_performance()
    demo_batch_processing()
    demo_image_object()
    
    print("✅ All demos completed successfully!")
    print()
    print("Generated files:")
    for f in ["scatter_demo.png", "multi_demo.png", "histogram_demo.png",
              "labeled_demo.png", "numpy_demo.png"]:
        if os.path.exists(f):
            size = os.path.getsize(f)
            print(f"   {f} ({size} bytes)")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
