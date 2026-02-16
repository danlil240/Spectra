#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace spectra::vk
{

struct PipelineConfig
{
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    const uint8_t* vert_spirv = nullptr;
    size_t vert_spirv_size = 0;
    const uint8_t* frag_spirv = nullptr;
    size_t frag_spirv_size = 0;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool enable_blending = true;
    std::vector<VkVertexInputBindingDescription> vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;
    // 3D pipeline extensions (defaults preserve existing 2D behavior)
    bool enable_depth_test = false;
    bool enable_depth_write = false;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_LESS;
    bool enable_backface_cull = false;
    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;
};

VkShaderModule create_shader_module(VkDevice device, const uint8_t* spirv, size_t size);

VkPipeline create_graphics_pipeline(VkDevice device, const PipelineConfig& config);

// Create descriptor set layout for frame UBO + series SSBO
VkDescriptorSetLayout create_frame_descriptor_layout(VkDevice device);
VkDescriptorSetLayout create_series_descriptor_layout(VkDevice device);

// Create pipeline layout with push constants
VkPipelineLayout create_pipeline_layout(VkDevice device,
                                        const std::vector<VkDescriptorSetLayout>& set_layouts);

}  // namespace spectra::vk
