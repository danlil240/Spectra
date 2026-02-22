#include "vk_pipeline.hpp"

#include <stdexcept>

#include "../backend.hpp"

namespace spectra::vk
{

VkShaderModule create_shader_module(VkDevice device, const uint8_t* spirv, size_t size)
{
    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode    = reinterpret_cast<const uint32_t*>(spirv);

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create shader module");
    }
    return module;
}

VkPipeline create_graphics_pipeline(VkDevice device, const PipelineConfig& config)
{
    VkShaderModule vert_module =
        create_shader_module(device, config.vert_spirv, config.vert_spirv_size);
    VkShaderModule frag_module =
        create_shader_module(device, config.frag_spirv, config.frag_spirv_size);

    VkPipelineShaderStageCreateInfo vert_stage{};
    vert_stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_module;
    vert_stage.pName  = "main";

    VkPipelineShaderStageCreateInfo frag_stage{};
    frag_stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_module;
    frag_stage.pName  = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vert_stage, frag_stage};

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount =
        static_cast<uint32_t>(config.vertex_bindings.size());
    vertex_input.pVertexBindingDescriptions = config.vertex_bindings.data();
    vertex_input.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(config.vertex_attributes.size());
    vertex_input.pVertexAttributeDescriptions = config.vertex_attributes.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = config.topology;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // Dynamic viewport, scissor, and line width
    VkDynamicState                   dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                                         VK_DYNAMIC_STATE_SCISSOR,
                                                         VK_DYNAMIC_STATE_LINE_WIDTH};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 3;
    dynamic_state.pDynamicStates    = dynamic_states;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth   = 1.0f;
    rasterizer.cullMode  = config.enable_backface_cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = config.msaa_samples;

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                      | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (config.enable_blending)
    {
        blend_attachment.blendEnable         = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments    = &blend_attachment;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable  = config.enable_depth_test ? VK_TRUE : VK_FALSE;
    depth_stencil.depthWriteEnable = config.enable_depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp   = config.depth_compare_op;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable     = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = stages;
    pipeline_info.pVertexInputState   = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState      = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState   = &multisampling;
    pipeline_info.pDepthStencilState  = &depth_stencil;
    pipeline_info.pColorBlendState    = &color_blending;
    pipeline_info.pDynamicState       = &dynamic_state;
    pipeline_info.layout              = config.pipeline_layout;
    pipeline_info.renderPass          = config.render_pass;
    pipeline_info.subpass             = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline)
        != VK_SUCCESS)
    {
        vkDestroyShaderModule(device, vert_module, nullptr);
        vkDestroyShaderModule(device, frag_module, nullptr);
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    vkDestroyShaderModule(device, vert_module, nullptr);
    vkDestroyShaderModule(device, frag_module, nullptr);

    return pipeline;
}

VkDescriptorSetLayout create_frame_descriptor_layout(VkDevice device)
{
    VkDescriptorSetLayoutBinding ubo_binding{};
    ubo_binding.binding         = 0;
    ubo_binding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings    = &ubo_binding;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create frame descriptor set layout");
    }
    return layout;
}

VkDescriptorSetLayout create_series_descriptor_layout(VkDevice device)
{
    VkDescriptorSetLayoutBinding ssbo_binding{};
    ssbo_binding.binding         = 0;
    ssbo_binding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ssbo_binding.descriptorCount = 1;
    ssbo_binding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings    = &ssbo_binding;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create series descriptor set layout");
    }
    return layout;
}

VkPipelineLayout create_pipeline_layout(VkDevice                                  device,
                                        const std::vector<VkDescriptorSetLayout>& set_layouts)
{
    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset     = 0;
    push_range.size       = sizeof(SeriesPushConstants);

    VkPipelineLayoutCreateInfo info{};
    info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount         = static_cast<uint32_t>(set_layouts.size());
    info.pSetLayouts            = set_layouts.data();
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges    = &push_range;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout");
    }
    return layout;
}

}   // namespace spectra::vk
