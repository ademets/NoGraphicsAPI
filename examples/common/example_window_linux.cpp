#include "example_support.hpp"

#include <NoGraphicsAPI/vulkan.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <assert.h>
#include <stdio.h>

namespace
{

VkResult create_surface(VkInstance instance, void* window, VkSurfaceKHR* surface) noexcept
{
    if (SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(window), instance, nullptr, surface)) return VK_SUCCESS;
    fprintf(stderr, "Vulkan surface creation failed: %s\n", SDL_GetError());
    return VK_ERROR_INITIALIZATION_FAILED;
}

} // namespace

double example_time_seconds() noexcept
{
    return double(SDL_GetTicksNS()) * 1.0e-9;
}

void* open_example_window(const char* title, uint32 width, uint32 height) noexcept
{
    assert(title && width && height);
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL video initialization failed: %s\n", SDL_GetError());
        return nullptr;
    }
    SDL_Window* window = SDL_CreateWindow(title, int(width), int(height), SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window)
    {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
    }
    return window;
}

gpu::DeviceInit create_example_device(void* window) noexcept
{
    if (!window) return {.error = gpu::Error::driver_error};
    uint32 extension_count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    if (!extensions)
    {
        fprintf(stderr, "Vulkan extension discovery failed: %s\n", SDL_GetError());
        return {.error = gpu::Error::driver_error};
    }
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(window), &width, &height))
    {
        fprintf(stderr, "Drawable size query failed: %s\n", SDL_GetError());
        return {.error = gpu::Error::driver_error};
    }
    const gpu::VulkanSurfaceDesc surface{
        .instance_extensions = {extensions, extension_count},
        .create_surface = create_surface,
        .user_data = window,
        .drawable_extent = {.x = uint32(width), .y = uint32(height)},
    };
    const gpu::DeviceInit result = gpu::create_device({.vulkan_surface = &surface, .swapchain_format = gpu::Format::bgra8_srgb});
    if (result.error != gpu::Error::none) fprintf(stderr, "Device creation failed: %u\n", unsigned(result.error));
    return result;
}

bool pump_example_window(void* window, gpu::Device* device) noexcept
{
    bool resized = false;
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
            (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) return false;
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_MINIMIZED ||
            event.type == SDL_EVENT_WINDOW_RESTORED || event.type == SDL_EVENT_WINDOW_MAXIMIZED) resized = true;
    }
    const bool minimized = (SDL_GetWindowFlags(static_cast<SDL_Window*>(window)) & SDL_WINDOW_MINIMIZED) != 0;
    if (resized)
    {
        int width = 0;
        int height = 0;
        if (!minimized) SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(window), &width, &height);
        gpu::set_drawable_extent(device, {.x = uint32(width), .y = uint32(height)});
    }
    if (minimized) SDL_Delay(16);
    return true;
}

void close_example_window(void*& window) noexcept
{
    SDL_DestroyWindow(static_cast<SDL_Window*>(window));
    window = nullptr;
    SDL_Quit();
}
