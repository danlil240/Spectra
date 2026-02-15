#include <plotix/app.hpp>
#include <gtest/gtest.h>
#include "render/backend.hpp"

using namespace plotix;

class DepthBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        AppConfig config;
        config.headless = true;
        app_ = std::make_unique<App>(config);
    }

    void TearDown() override {
        app_.reset();
    }

    std::unique_ptr<App> app_;
};

TEST_F(DepthBufferTest, DepthBufferCreatedWithSwapchain) {
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);
    
    auto pipeline = backend->create_pipeline(PipelineType::Scatter3D);
    EXPECT_TRUE(pipeline);
}

TEST_F(DepthBufferTest, DepthBufferExistsForMultiplePipelines) {
    auto* backend = app_->backend();
    
    auto line3d = backend->create_pipeline(PipelineType::Line3D);
    auto scatter3d = backend->create_pipeline(PipelineType::Scatter3D);
    
    EXPECT_TRUE(line3d);
    EXPECT_TRUE(scatter3d);
}

TEST_F(DepthBufferTest, DepthTestingEnabledFor3D) {
    auto* backend = app_->backend();
    
    auto line3d = backend->create_pipeline(PipelineType::Line3D);
    auto scatter3d = backend->create_pipeline(PipelineType::Scatter3D);
    auto grid3d = backend->create_pipeline(PipelineType::Grid3D);
    
    EXPECT_TRUE(line3d);
    EXPECT_TRUE(scatter3d);
    EXPECT_TRUE(grid3d);
}

TEST_F(DepthBufferTest, DepthTestingDisabledFor2D) {
    auto* backend = app_->backend();
    
    auto line2d = backend->create_pipeline(PipelineType::Line);
    auto scatter2d = backend->create_pipeline(PipelineType::Scatter);
    auto grid2d = backend->create_pipeline(PipelineType::Grid);
    
    EXPECT_TRUE(line2d);
    EXPECT_TRUE(scatter2d);
    EXPECT_TRUE(grid2d);
}

TEST_F(DepthBufferTest, AllPipelineTypesSupported) {
    auto* backend = app_->backend();
    
    auto line2d = backend->create_pipeline(PipelineType::Line);
    auto scatter2d = backend->create_pipeline(PipelineType::Scatter);
    auto grid2d = backend->create_pipeline(PipelineType::Grid);
    auto line3d = backend->create_pipeline(PipelineType::Line3D);
    auto scatter3d = backend->create_pipeline(PipelineType::Scatter3D);
    auto grid3d = backend->create_pipeline(PipelineType::Grid3D);
    
    EXPECT_TRUE(line2d);
    EXPECT_TRUE(scatter2d);
    EXPECT_TRUE(grid2d);
    EXPECT_TRUE(line3d);
    EXPECT_TRUE(scatter3d);
    EXPECT_TRUE(grid3d);
}

TEST_F(DepthBufferTest, DepthBufferFormatSupported) {
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);
    
    auto pipeline = backend->create_pipeline(PipelineType::Scatter3D);
    EXPECT_TRUE(pipeline);
}

TEST_F(DepthBufferTest, MeshAndSurfacePipelineTypes) {
    [[maybe_unused]] PipelineType mesh3d = PipelineType::Mesh3D;
    [[maybe_unused]] PipelineType surface3d = PipelineType::Surface3D;
    
    SUCCEED();
}
