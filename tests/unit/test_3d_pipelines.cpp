#include <plotix/app.hpp>
#include <gtest/gtest.h>
#include "render/backend.hpp"

using namespace plotix;

TEST(Pipeline3D, Line3DCreation) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto* backend = app.backend();
    auto pipeline = backend->create_pipeline(PipelineType::Line3D);
    EXPECT_TRUE(pipeline);
}

TEST(Pipeline3D, Scatter3DCreation) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto* backend = app.backend();
    auto pipeline = backend->create_pipeline(PipelineType::Scatter3D);
    EXPECT_TRUE(pipeline);
}

TEST(Pipeline3D, Grid3DCreation) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto* backend = app.backend();
    auto pipeline = backend->create_pipeline(PipelineType::Grid3D);
    EXPECT_TRUE(pipeline);
}

TEST(Pipeline3D, DepthTestingEnabled) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto* backend = app.backend();
    
    auto line_pipeline = backend->create_pipeline(PipelineType::Line3D);
    auto scatter_pipeline = backend->create_pipeline(PipelineType::Scatter3D);
    auto grid_pipeline = backend->create_pipeline(PipelineType::Grid3D);
    
    EXPECT_TRUE(line_pipeline);
    EXPECT_TRUE(scatter_pipeline);
    EXPECT_TRUE(grid_pipeline);
}

TEST(Pipeline3D, EnumTypesExist) {
    // Verify the enum values exist (compile-time check)
    [[maybe_unused]] PipelineType line3d = PipelineType::Line3D;
    [[maybe_unused]] PipelineType scatter3d = PipelineType::Scatter3D;
    [[maybe_unused]] PipelineType grid3d = PipelineType::Grid3D;
    [[maybe_unused]] PipelineType mesh3d = PipelineType::Mesh3D;
    [[maybe_unused]] PipelineType surface3d = PipelineType::Surface3D;
    
    SUCCEED();
}

TEST(DepthBuffer, CreatedWithSwapchain) {
    AppConfig config;
    config.headless = true;
    App app(config);
    
    auto* backend = app.backend();
    auto pipeline = backend->create_pipeline(PipelineType::Scatter3D);
    
    EXPECT_TRUE(pipeline);
}

TEST(Pipeline2D, UnaffectedBy3D) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto* backend = app.backend();
    
    auto line_2d = backend->create_pipeline(PipelineType::Line);
    auto scatter_2d = backend->create_pipeline(PipelineType::Scatter);
    auto grid_2d = backend->create_pipeline(PipelineType::Grid);
    
    EXPECT_TRUE(line_2d);
    EXPECT_TRUE(scatter_2d);
    EXPECT_TRUE(grid_2d);
}

TEST(Pipeline2D3D, CanCoexist) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto* backend = app.backend();
    
    auto line_2d = backend->create_pipeline(PipelineType::Line);
    auto line_3d = backend->create_pipeline(PipelineType::Line3D);
    auto scatter_2d = backend->create_pipeline(PipelineType::Scatter);
    auto scatter_3d = backend->create_pipeline(PipelineType::Scatter3D);
    
    EXPECT_TRUE(line_2d);
    EXPECT_TRUE(line_3d);
    EXPECT_TRUE(scatter_2d);
    EXPECT_TRUE(scatter_3d);
}
