#include "helix_nebula_shared.h"
#include "example_support.hpp"

#include <NoGraphicsAPIUtility/bump_allocator.hpp>
#include <NoGraphicsAPIUtility/upload_queue.hpp>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace gpu;

namespace
{

constexpr uint32 frames_in_flight = 2;
constexpr uint32 noise_extent = 64;
constexpr uint64 noise_byte_count = uint64(noise_extent) * noise_extent * noise_extent * 4;
constexpr TextureDesc noise_description{
    .type = TextureType::three_d,
    .extent = {.x = noise_extent, .y = noise_extent, .z = noise_extent},
    .usage = TextureUsage::sampled | TextureUsage::transfer_destination,
};

static_assert(sizeof(HelixNebulaRoot) == 48);

struct Resources
{
    void* window = nullptr;
    Device* device = nullptr;
    PSO* pso = nullptr;
    GpuHeap root_heap{};
    GpuCpuRange<HelixNebulaRoot> roots[frames_in_flight]{};
    TextureHeap texture_heap{};
    Texture* noise = nullptr;
    TextureDescriptorHeap* textures = nullptr;
    SamplerDescriptorHeap* samplers = nullptr;
    TimelinePoint completion{};
    CommandPool* command_pools[frames_in_flight]{};
};

Span<byte> load_shader(const char* path) noexcept
{
    FILE* file = fopen(path, "rb");
    if (!file)
    {
        perror(path);
        return {};
    }
    if (fseek(file, 0, SEEK_END) != 0 || ftell(file) <= 0)
    {
        fprintf(stderr, "Cannot read shader: %s\n", path);
        fclose(file);
        return {};
    }
    const size_t size = size_t(ftell(file));
    rewind(file);
    Span<byte> code{static_cast<byte*>(malloc(size)), size};
    if (fread(code.data, 1, code.size, file) != code.size)
    {
        fprintf(stderr, "Cannot read shader: %s\n", path);
        free(code.data);
        code = {};
    }
    fclose(file);
    return code;
}

bool initialize(Resources& resources) noexcept
{
    resources.window = open_example_window("NoGraphicsAPI Helix Nebula", 1280, 900);
    if (!resources.window) return false;
    resources.device = create_example_device(resources.window).device;
    if (!resources.device) return false;
    printf("Using %s\n", get_device_caps(resources.device).device_name);

    const Span<byte> vertex_code = load_shader(NOGRAPHICSAPI_HELIX_VERTEX_SHADER_PATH);
    if (!vertex_code.data) return false;
    const Span<byte> fragment_code = load_shader(NOGRAPHICSAPI_HELIX_FRAGMENT_SHADER_PATH);
    if (!fragment_code.data)
    {
        free(vertex_code.data);
        return false;
    }
    resources.pso = create_graphics_pso(resources.device, {
        .vertex = {.code = {vertex_code.data, vertex_code.size}, .entry_point = "vertexMain"},
        .fragment = {.code = {fragment_code.data, fragment_code.size}, .entry_point = "fragmentMain"},
        .color_targets = {{.format = Format::bgra8_srgb}},
    });
    free(fragment_code.data);
    free(vertex_code.data);
    if (!resources.pso) return false;

    resources.root_heap = create_gpu_heap(resources.device, 4096);
    if (!resources.root_heap.owner) return false;
    BumpAllocator roots(resources.root_heap.range);
    for (uint32 i = 0; i != frames_in_flight; ++i)
        resources.roots[i] = roots.allocate<HelixNebulaRoot>();

    resources.completion.semaphore = create_timeline_semaphore(resources.device);
    if (!resources.completion.semaphore) return false;
    for (uint32 i = 0; i != frames_in_flight; ++i)
    {
        resources.command_pools[i] = create_command_pool(resources.device);
        if (!resources.command_pools[i]) return false;
    }

    const SizeAlign noise_requirements = get_texture_size_align(resources.device, noise_description);
    if (!noise_requirements.size) return false;
    resources.texture_heap = create_texture_heap(resources.device, noise_requirements.size);
    if (!resources.texture_heap.owner) return false;
    resources.noise = create_texture(resources.device, noise_description, resources.texture_heap, 0);
    if (!resources.noise) return false;
    resources.textures = create_texture_descriptor_heap(resources.device, 1);
    if (!resources.textures) return false;
    resources.samplers = create_sampler_descriptor_heap(resources.device, 1);
    if (!resources.samplers) return false;
    write_texture_descriptor(resources.textures, 0, resources.noise, TextureDescriptorType::sampled);
    write_sampler_descriptor(resources.samplers, 0);

    UploadQueue uploads(resources.device, noise_byte_count);
    if (!uploads.valid()) return false;
    CommandBuffer* commands = begin_commands(resources.command_pools[0]);
    if (!commands) return false;
    initialize_texture(commands, resources.noise);
    end_commands(commands);
    ++resources.completion.value;
    submit(resources.device, {.commands = {commands}, .completion = resources.completion});

    byte* noise = static_cast<byte*>(malloc(size_t(noise_byte_count)));
    uint32 random = 0x7293b51du;
    for (uint64 i = 0; i != noise_byte_count; ++i)
    {
        random ^= random << 13u;
        random ^= random >> 17u;
        random ^= random << 5u;
        noise[i] = byte(random >> 24u);
    }
    uploads.upload_texture(resources.noise, {noise, size_t(noise_byte_count)});
    free(noise);
    uploads.wait();
    return true;
}

void destroy(Resources& resources) noexcept
{
    if (resources.device) wait_idle(resources.device);
    for (uint32 i = 0; i != frames_in_flight; ++i)
        destroy_command_pool(resources.command_pools[i]);
    destroy_timeline_semaphore(resources.completion.semaphore);
    destroy_sampler_descriptor_heap(resources.samplers);
    destroy_texture_descriptor_heap(resources.textures);
    destroy_texture(resources.noise);
    destroy_texture_heap(resources.texture_heap);
    destroy_gpu_heap(resources.root_heap);
    destroy_pso(resources.pso);
    destroy_device(resources.device);
    close_example_window(resources.window);
}

} // namespace

int main(int argc, char** argv)
{
    uint64 frame_limit = 0;
    for (int32 i = 1; i != argc; ++i)
    {
        if (strcmp(argv[i], "--help") == 0)
        {
            printf("Usage: %s [--frames count]\n", argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
        {
            char* end = nullptr;
            errno = 0;
            frame_limit = strtoull(argv[++i], &end, 10);
            if (errno == ERANGE || argv[i][0] < '0' || argv[i][0] > '9' || !frame_limit || *end)
            {
                fprintf(stderr, "Invalid frame count: %s\n", argv[i]);
                return 1;
            }
        }
        else
        {
            fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]);
            return 1;
        }
    }

    Resources resources{};
    if (!initialize(resources))
    {
        fprintf(stderr, "Helix Nebula initialization failed.\n");
        destroy(resources);
        return 1;
    }

    uint64 submitted_frames = 0;
    const double start = example_time_seconds();
    while ((!frame_limit || submitted_frames < frame_limit) && pump_example_window(resources.window, resources.device))
    {
        if (resources.completion.value >= frames_in_flight)
            wait_timeline({.semaphore = resources.completion.semaphore, .value = resources.completion.value - frames_in_flight + 1});
        const uint32 frame_index = uint32(resources.completion.value % frames_in_flight);
        reset_command_pool(resources.command_pools[frame_index]);
        CommandBuffer* commands = begin_commands(resources.command_pools[frame_index]);
        const SwapchainFrame frame = acquire(commands);
        if (!frame.render_view) continue;

        const float time = float(example_time_seconds() - start);
        const float tilt = 0.62f + 0.035f * sinf(time * 0.045f);
        const HelixNebulaRoot root{
            .inverse_extent = {.x = 1.0f / float(frame.extent.x), .y = 1.0f / float(frame.extent.y)},
            .aspect = float(frame.extent.x) / float(frame.extent.y),
            .view_scale = 1.68f,
            .orientation = {.x = cosf(tilt), .y = sinf(tilt), .z = cosf(0.55f), .w = sinf(0.55f)},
        };
        memcpy(resources.roots[frame_index].cpu, &root, sizeof(root));

        set_texture_descriptor_heap(commands, resources.textures);
        set_sampler_descriptor_heap(commands, resources.samplers);
        begin_render_pass(commands, {.colors = {{.render_view = frame.render_view, .load = LoadOp::discard}}});
        bind_pso(commands, resources.pso);
        draw(commands, resources.roots[frame_index].gpu, 3);
        end_render_pass(commands);
        end_commands(commands);
        ++resources.completion.value;
        submit_and_present(resources.device, {.commands = {commands}, .completion = resources.completion});
        ++submitted_frames;
    }

    destroy(resources);
    return 0;
}
