#include <plotix/plotix.hpp>
#include <iostream>

using namespace plotix;

int main() {
    std::cout << "Axes3D Demo - Week 3 Agent 3 Implementation\n";
    std::cout << "============================================\n\n";
    
    try {
        App app;
        auto& fig = app.figure();
        
        // Create a 3D subplot
        auto& ax = fig.subplot3d(1, 1, 1);
        
        // Configure the 3D axes
        ax.xlim(-5.0f, 5.0f);
        ax.ylim(-5.0f, 5.0f);
        ax.zlim(-5.0f, 5.0f);
        
        ax.xlabel("X Axis");
        ax.ylabel("Y Axis");
        ax.zlabel("Z Axis");
        ax.title("3D Coordinate System");
        
        // Configure grid planes
        ax.grid_planes(Axes3D::GridPlane::XY | Axes3D::GridPlane::XZ);
        ax.show_bounding_box(true);
        ax.grid(true);
        
        // Configure camera
        ax.camera()
            .set_azimuth(45.0f)
            .set_elevation(30.0f)
            .set_distance(15.0f);
        
        std::cout << "✓ Created 3D axes with:\n";
        std::cout << "  - X limits: [-5, 5]\n";
        std::cout << "  - Y limits: [-5, 5]\n";
        std::cout << "  - Z limits: [-5, 5]\n";
        std::cout << "  - Camera azimuth: 45°\n";
        std::cout << "  - Camera elevation: 30°\n";
        std::cout << "  - Grid planes: XY + XZ\n";
        std::cout << "  - Bounding box: enabled\n\n";
        
        // Test tick computation
        auto x_ticks = ax.compute_x_ticks();
        auto y_ticks = ax.compute_y_ticks();
        auto z_ticks = ax.compute_z_ticks();
        
        std::cout << "✓ Computed ticks:\n";
        std::cout << "  - X ticks: " << x_ticks.positions.size() << " positions\n";
        std::cout << "  - Y ticks: " << y_ticks.positions.size() << " positions\n";
        std::cout << "  - Z ticks: " << z_ticks.positions.size() << " positions\n\n";
        
        // Test auto-fit
        ax.auto_fit();
        std::cout << "✓ Auto-fit completed\n";
        std::cout << "  - Camera target: (" 
                  << ax.camera().target.x << ", "
                  << ax.camera().target.y << ", "
                  << ax.camera().target.z << ")\n\n";
        
        std::cout << "✅ Axes3D demo completed successfully!\n";
        std::cout << "\nNote: This demo tests the Axes3D infrastructure.\n";
        std::cout << "Full rendering requires Agent 4 (3D pipelines) and Agent 5 (3D series).\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << "\n";
        return 1;
    }
}
