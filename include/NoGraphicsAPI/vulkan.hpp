#pragma once

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <vulkan/vulkan.h>

namespace gpu
{

// Pass through DeviceDesc::vulkan_surface with window == nullptr. Read only during create_device.
// The required callback creates a surface for the supplied instance using the default Vulkan allocator.
// On failure, return the Vulkan error and leave the output null.
// On success, NoGraphicsAPI owns the surface, including cleanup after later initialization failures.
// The native window must outlive the device. The callback and user_data are not retained.
struct VulkanSurfaceDesc
{
    Span<const char* const> instance_extensions = {}; // At most 249 names; duplicates and common presentation extensions are accepted.
    VkResult (*create_surface)(VkInstance instance, void* user_data, VkSurfaceKHR* surface) noexcept = nullptr;
    void* user_data = nullptr;
    uint32x2 drawable_extent = {}; // Pixels; zero defers swapchain creation until a nonzero extent is supplied.
};

// For devices created with VulkanSurfaceDesc. Call on the presentation thread, outside an acquired frame.
// Supply the window's current pixel size after event processing. Zero suspends acquisition; no GPU wait is needed.
// Fixed-size surfaces use Vulkan's dimensions when the supplied extent is nonzero.
void set_drawable_extent(Device* device, uint32x2 extent) noexcept;

} // namespace gpu
