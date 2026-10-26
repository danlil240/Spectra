#pragma once

// vk_frame_utils.hpp — Pure (Vulkan-free) helpers used by the Vulkan backend.
// Kept header-only and free of Vulkan types so the logic can be unit tested
// without a GPU.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace spectra::vk
{

// A deferred GPU resource destroyed when the backend's submission counter was
// `frame_destroyed` can only be referenced by command buffers whose submission
// index is <= frame_destroyed + 1 (the one being recorded right now).  All
// windows share one graphics queue, and a fence signal from vkQueueSubmit
// covers every earlier submission on that queue, so the resource is safe to
// free once a fence for a later submission has been observed signalled.
inline bool deferred_free_is_safe(uint64_t frame_destroyed, uint64_t completed_submit)
{
    return completed_submit > frame_destroyed;
}

// Clamp a requested rasterization line width to what the device accepts.
// Without the wideLines feature, only 1.0 is valid
// (VUID-vkCmdSetLineWidth-lineWidth-00788).
inline float clamp_line_width(float width,
                              bool  wide_lines_enabled,
                              float range_min,
                              float range_max)
{
    if (!wide_lines_enabled || !(width == width))   // NaN -> 1.0
        return 1.0f;
    if (range_max < range_min)
        std::swap(range_min, range_max);
    return std::clamp(width, range_min, range_max);
}

// Copy a tightly packed RGBA8 readback of src_w x src_h pixels into a caller
// buffer laid out as dst_w x dst_h pixels (dst_w/dst_h >= src_w/src_h).  Rows
// keep the caller's stride and any pixels outside the copied area are zeroed,
// so a readback clamped to a smaller surface never shears the image.
// Optionally swaps bytes 0 and 2 of each copied pixel (BGRA -> RGBA).
inline void copy_readback_rows(uint8_t*       dst,
                               uint32_t       dst_w,
                               uint32_t       dst_h,
                               const uint8_t* src,
                               uint32_t       src_w,
                               uint32_t       src_h,
                               bool           swap_red_blue)
{
    if (!dst)
        return;
    const uint32_t copy_w     = std::min(src_w, dst_w);
    const uint32_t copy_h     = std::min(src_h, dst_h);
    const size_t   dst_stride = static_cast<size_t>(dst_w) * 4;
    const size_t   src_stride = static_cast<size_t>(src_w) * 4;

    for (uint32_t y = 0; y < dst_h; ++y)
    {
        uint8_t* drow = dst + static_cast<size_t>(y) * dst_stride;
        if (y >= copy_h || !src)
        {
            std::memset(drow, 0, dst_stride);
            continue;
        }
        const uint8_t* srow = src + static_cast<size_t>(y) * src_stride;
        if (drow != srow)
            std::memmove(drow, srow, static_cast<size_t>(copy_w) * 4);
        if (copy_w < dst_w)
            std::memset(drow + static_cast<size_t>(copy_w) * 4,
                        0,
                        static_cast<size_t>(dst_w - copy_w) * 4);
        if (swap_red_blue)
        {
            for (uint32_t x = 0; x < copy_w; ++x)
                std::swap(drow[x * 4 + 0], drow[x * 4 + 2]);
        }
    }
}

// Decide what to do before overwriting a dynamic-UBO ring slot.
//   last_use        : submission index that last used the slot (0 = never)
//   pending_submit  : submission index the new write will be part of
//   completed_submit: highest submission index known to have finished on GPU
enum class UboSlotReuse
{
    Free,            // slot unused or its last user has completed
    WaitForGpu,      // slot still read by an earlier, in-flight submission
    FrameOverflow,   // slot already used by the submission being recorded
};

inline UboSlotReuse classify_ubo_slot_reuse(uint64_t last_use,
                                            uint64_t pending_submit,
                                            uint64_t completed_submit)
{
    if (last_use == 0 || last_use <= completed_submit)
        return UboSlotReuse::Free;
    if (last_use >= pending_submit)
        return UboSlotReuse::FrameOverflow;
    return UboSlotReuse::WaitForGpu;
}

}   // namespace spectra::vk
