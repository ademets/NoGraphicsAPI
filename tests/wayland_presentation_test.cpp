#include <NoGraphicsAPI/vulkan.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stdio.h>
#include <string.h>

namespace
{

int failures = 0;
bool expected_error = false;

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #expression); ++failures; } } while (0)

struct SurfaceContext
{
    SDL_Window* window = nullptr;
    uint32 calls = 0;
    bool fail = false;
};

void receive_error(const char* message, bool) noexcept
{
    if (expected_error) return;
    fprintf(stderr, "%s\n", message);
    ++failures;
}

VkResult create_surface(VkInstance instance, void* user_data, VkSurfaceKHR* surface) noexcept
{
    SurfaceContext& context = *static_cast<SurfaceContext*>(user_data);
    ++context.calls;
    if (context.fail) return VK_ERROR_INITIALIZATION_FAILED;
    if (SDL_Vulkan_CreateSurface(context.window, instance, nullptr, surface)) return VK_SUCCESS;
    fprintf(stderr, "SDL_Vulkan_CreateSurface: %s\n", SDL_GetError());
    return VK_ERROR_INITIALIZATION_FAILED;
}

int test_presentation(SDL_Window* window, gpu::Span<const char* const> extensions) noexcept
{
    SurfaceContext context{.window = window};
    const char* missing_extensions[]{"VK_NOGRAPHICSAPI_missing_surface_extension"};
    const gpu::VulkanSurfaceDesc missing_surface{
        .instance_extensions = missing_extensions,
        .create_surface = create_surface,
        .user_data = &context,
    };
    expected_error = true;
    gpu::DeviceInit init = gpu::create_device({.vulkan_surface = &missing_surface});
    CHECK(init.error == gpu::Error::unsupported && !init.device && context.calls == 0);
    gpu::destroy_device(init.device);

    const gpu::VulkanSurfaceDesc surface{
        .instance_extensions = extensions,
        .create_surface = create_surface,
        .user_data = &context,
    };
    context.fail = true;
    init = gpu::create_device({.vulkan_surface = &surface, .swapchain_format = gpu::Format::bgra8_srgb});
    CHECK(init.error == gpu::Error::driver_error && !init.device && context.calls == 1);
    gpu::destroy_device(init.device);
    context.fail = false;

    init = gpu::create_device({.vulkan_surface = &surface, .swapchain_format = gpu::Format::d32_float});
    CHECK(init.error == gpu::Error::unsupported && !init.device && context.calls == 2);
    gpu::destroy_device(init.device);
    expected_error = false;

    init = gpu::create_device({.vulkan_surface = &surface, .swapchain_format = gpu::Format::bgra8_srgb});
    if (!init.device)
    {
        fprintf(stderr, "Wayland device initialization: %u\n", unsigned(init.error));
        return 1;
    }
    CHECK(context.calls == 3);
    gpu::Device* device = init.device;
    gpu::CommandPool* pools[]{gpu::create_command_pool(device), gpu::create_command_pool(device)};
    gpu::TimelinePoint completion{.semaphore = gpu::create_timeline_semaphore(device)};
    if (!pools[0] || !pools[1] || !completion.semaphore) ++failures;
    else
    {
        CHECK(gpu::get_drawable_extent(device).x == 0);
        gpu::CommandBuffer* commands = gpu::begin_commands(pools[0]);
        CHECK(!gpu::acquire(commands).render_view);
        gpu::end_commands(commands);

        for (uint32 frame_index = 0; frame_index < 12; ++frame_index)
        {
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {}
            if (completion.value >= 2)
                gpu::wait_timeline({.semaphore = completion.semaphore, .value = completion.value - 1});
            gpu::CommandPool* pool = pools[completion.value % 2];
            gpu::reset_command_pool(pool);

            if (frame_index == 4)
            {
                gpu::set_drawable_extent(device, {});
                const gpu::uint32x2 extent = gpu::get_drawable_extent(device);
                CHECK(extent.x == 0 && extent.y == 0);
                commands = gpu::begin_commands(pool);
                const gpu::SwapchainFrame empty = gpu::acquire(commands);
                CHECK(!empty.render_view && empty.extent.x == 0 && empty.extent.y == 0);
                gpu::end_commands(commands);
                gpu::reset_command_pool(pool);
            }
            if (frame_index == 6)
            {
                gpu::wait_idle(device);
                expected_error = true;
                CHECK(gpu::set_swapchain_format(device, gpu::Format::d32_float, gpu::ColorSpace::srgb) == gpu::Error::unsupported);
                expected_error = false;
            }
            const gpu::uint32x2 requested = frame_index < 2 || frame_index >= 8
                ? gpu::uint32x2{.x = 320, .y = 240} : gpu::uint32x2{.x = 384, .y = 256};
            if (frame_index == 2 || frame_index == 8) SDL_SetWindowSize(window, int(requested.x), int(requested.y));
            gpu::set_drawable_extent(device, requested);
            const gpu::uint32x2 extent = gpu::get_drawable_extent(device);
            CHECK(extent.x == requested.x && extent.y == requested.y);
            commands = gpu::begin_commands(pool);
            const gpu::SwapchainFrame frame = gpu::acquire(commands);
            CHECK(frame.render_view && frame.extent.x == requested.x && frame.extent.y == requested.y);
            if (!frame.render_view) break;
            gpu::begin_render_pass(commands, {.colors = {{
                .render_view = frame.render_view,
                .load = gpu::LoadOp::clear,
                .clear = {.x = float(frame_index) / 12.0f, .y = 0.25f, .z = 0.5f, .w = 1.0f},
            }}});
            gpu::end_render_pass(commands);
            gpu::end_commands(commands);
            ++completion.value;
            gpu::submit_and_present(device, {.commands = {commands}, .completion = completion});
        }
    }

    gpu::wait_idle(device);
    gpu::destroy_command_pool(pools[1]);
    gpu::destroy_command_pool(pools[0]);
    gpu::destroy_timeline_semaphore(completion.semaphore);
    gpu::destroy_device(device);
    if (!failures) puts("Native Wayland surface creation, failure recovery, resize, zero drawable, and presentation checks passed.");
    return failures ? 1 : 0;
}

} // namespace

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SKIP: SDL video initialization: %s\n", SDL_GetError());
        return 77;
    }
    if (strcmp(SDL_GetCurrentVideoDriver(), "wayland") != 0)
    {
        puts("SKIP: this test requires native Wayland.");
        SDL_Quit();
        return 77;
    }
    SDL_Window* window = SDL_CreateWindow("NoGraphicsAPI Wayland presentation test", 320, 240, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    uint32 extension_count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    int result = 1;
    if (!extensions) fprintf(stderr, "SDL_Vulkan_GetInstanceExtensions: %s\n", SDL_GetError());
    else
    {
        gpu::set_error_callback(receive_error);
        result = test_presentation(window, {extensions, extension_count});
        gpu::set_error_callback(nullptr);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
