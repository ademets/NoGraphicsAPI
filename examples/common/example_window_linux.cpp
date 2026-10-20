#include "example_window_wayland.hpp"
#include "xdg-shell-client-protocol.h"

#include <vulkan/vulkan_wayland.h>
#include <wayland-client.h>
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace
{

struct WaylandWindow
{
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    xdg_wm_base* shell = nullptr;
    wl_surface* surface = nullptr;
    xdg_surface* shell_surface = nullptr;
    xdg_toplevel* toplevel = nullptr;
    gpu::uint32x2 extent = {};
    gpu::uint32x2 pending_extent = {};
    uint32 scale = 1;
    bool configured = false;
    bool resized = false;
    bool closed = false;
};

void destroy_window(WaylandWindow* window) noexcept
{
    if (window->toplevel) xdg_toplevel_destroy(window->toplevel);
    if (window->shell_surface) xdg_surface_destroy(window->shell_surface);
    if (window->surface) wl_surface_destroy(window->surface);
    if (window->shell) xdg_wm_base_destroy(window->shell);
    if (window->compositor) wl_compositor_destroy(window->compositor);
    if (window->registry) wl_registry_destroy(window->registry);
    if (window->display) wl_display_disconnect(window->display);
    delete window;
}

void shell_ping(void*, xdg_wm_base* shell, uint32 serial) noexcept
{
    xdg_wm_base_pong(shell, serial);
}

const xdg_wm_base_listener shell_listener{.ping = shell_ping};

void registry_global(void* data, wl_registry* registry, uint32 name, const char* interface, uint32 version) noexcept
{
    WaylandWindow& window = *static_cast<WaylandWindow*>(data);
    if (strcmp(interface, wl_compositor_interface.name) == 0 && version >= 6)
        window.compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 6));
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
    {
        window.shell = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(window.shell, &shell_listener, &window);
    }
}

void registry_remove(void*, wl_registry*, uint32) noexcept {}

const wl_registry_listener registry_listener{.global = registry_global, .global_remove = registry_remove};

void surface_output(void*, wl_surface*, wl_output*) noexcept {}

void surface_scale(void* data, wl_surface*, int32 scale) noexcept
{
    WaylandWindow& window = *static_cast<WaylandWindow*>(data);
    window.scale = uint32(scale);
    window.resized = true;
}

void surface_transform(void*, wl_surface*, uint32) noexcept {}

const wl_surface_listener surface_listener{
    .enter = surface_output,
    .leave = surface_output,
    .preferred_buffer_scale = surface_scale,
    .preferred_buffer_transform = surface_transform,
};

void surface_configure(void* data, xdg_surface* surface, uint32 serial) noexcept
{
    WaylandWindow& window = *static_cast<WaylandWindow*>(data);
    window.extent = window.pending_extent;
    window.configured = true;
    window.resized = true;
    xdg_surface_ack_configure(surface, serial);
}

const xdg_surface_listener surface_configure_listener{.configure = surface_configure};

void toplevel_configure(void* data, xdg_toplevel*, int32 width, int32 height, wl_array*) noexcept
{
    WaylandWindow& window = *static_cast<WaylandWindow*>(data);
    window.pending_extent = {
        .x = width ? uint32(width) : window.extent.x,
        .y = height ? uint32(height) : window.extent.y,
    };
}

void toplevel_close(void* data, xdg_toplevel*) noexcept
{
    static_cast<WaylandWindow*>(data)->closed = true;
}

const xdg_toplevel_listener toplevel_listener{.configure = toplevel_configure, .close = toplevel_close};

VkResult create_surface(VkInstance instance, void* data, VkSurfaceKHR* surface) noexcept
{
    const WaylandWindow& window = *static_cast<WaylandWindow*>(data);
    const VkWaylandSurfaceCreateInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = window.display,
        .surface = window.surface,
    };
    return vkCreateWaylandSurfaceKHR(instance, &info, nullptr, surface);
}

} // namespace

double example_time_seconds() noexcept
{
    timespec time{};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return double(time.tv_sec) + double(time.tv_nsec) * 1.0e-9;
}

void* open_example_window(const char* title, uint32 width, uint32 height) noexcept
{
    assert(title && width && height);
    WaylandWindow* window = new WaylandWindow{.extent = {.x = width, .y = height}, .pending_extent = {.x = width, .y = height}};
    window->display = wl_display_connect(nullptr);
    if (!window->display)
    {
        fprintf(stderr, "Wayland display connection failed: %s\n", strerror(errno));
        destroy_window(window);
        return nullptr;
    }
    window->registry = wl_display_get_registry(window->display);
    wl_registry_add_listener(window->registry, &registry_listener, window);
    if (wl_display_roundtrip(window->display) < 0 || !window->compositor || !window->shell)
    {
        fprintf(stderr, "Wayland initialization requires wl_compositor v6 and xdg_wm_base v1.\n");
        destroy_window(window);
        return nullptr;
    }
    window->surface = wl_compositor_create_surface(window->compositor);
    wl_surface_add_listener(window->surface, &surface_listener, window);
    window->shell_surface = xdg_wm_base_get_xdg_surface(window->shell, window->surface);
    xdg_surface_add_listener(window->shell_surface, &surface_configure_listener, window);
    window->toplevel = xdg_surface_get_toplevel(window->shell_surface);
    xdg_toplevel_add_listener(window->toplevel, &toplevel_listener, window);
    xdg_toplevel_set_title(window->toplevel, title);
    xdg_toplevel_set_app_id(window->toplevel, "NoGraphicsAPI");
    wl_surface_commit(window->surface);
    while (!window->configured && !window->closed)
    {
        if (wl_display_dispatch(window->display) < 0)
        {
            fprintf(stderr, "Wayland initial configure failed.\n");
            destroy_window(window);
            return nullptr;
        }
    }
    if (window->closed)
    {
        destroy_window(window);
        return nullptr;
    }
    wl_surface_set_buffer_scale(window->surface, int32(window->scale));
    return window;
}

gpu::VulkanSurfaceDesc example_wayland_surface(void* data) noexcept
{
    const WaylandWindow& window = *static_cast<WaylandWindow*>(data);
    static const char* const extensions[]{VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME};
    return {
        .instance_extensions = extensions,
        .create_surface = create_surface,
        .user_data = data,
        .drawable_extent = {.x = window.extent.x * window.scale, .y = window.extent.y * window.scale},
    };
}

gpu::DeviceInit create_example_device(void* window) noexcept
{
    if (!window) return {.error = gpu::Error::driver_error};
    const gpu::VulkanSurfaceDesc surface = example_wayland_surface(window);
    const gpu::DeviceInit result = gpu::create_device({.vulkan_surface = &surface, .swapchain_format = gpu::Format::bgra8_srgb});
    if (result.error != gpu::Error::none) fprintf(stderr, "Device creation failed: %u\n", unsigned(result.error));
    return result;
}

bool pump_example_window(void* data, gpu::Device* device) noexcept
{
    WaylandWindow& window = *static_cast<WaylandWindow*>(data);
    while (wl_display_prepare_read(window.display) != 0)
        if (wl_display_dispatch_pending(window.display) < 0) return false;

    pollfd display{.fd = wl_display_get_fd(window.display), .events = POLLIN};
    if (wl_display_flush(window.display) < 0)
    {
        if (errno != EAGAIN)
        {
            wl_display_cancel_read(window.display);
            return false;
        }
        display.events |= POLLOUT;
    }
    const int ready = poll(&display, 1, 0);
    if (ready > 0 && (display.revents & POLLIN))
    {
        if (wl_display_read_events(window.display) < 0) return false;
    }
    else wl_display_cancel_read(window.display);
    if ((ready < 0 && errno != EINTR) || (display.revents & (POLLERR | POLLHUP | POLLNVAL))) return false;
    if ((display.revents & POLLOUT) && wl_display_flush(window.display) < 0 && errno != EAGAIN) return false;
    if (wl_display_dispatch_pending(window.display) < 0 || window.closed) return false;

    if (window.resized)
    {
        // Vulkan commits this scale and the acknowledged configure with the next presented buffer.
        wl_surface_set_buffer_scale(window.surface, int32(window.scale));
        gpu::set_drawable_extent(device, {.x = window.extent.x * window.scale, .y = window.extent.y * window.scale});
        window.resized = false;
    }
    return true;
}

void close_example_window(void*& window) noexcept
{
    if (window) destroy_window(static_cast<WaylandWindow*>(window));
    window = nullptr;
}
