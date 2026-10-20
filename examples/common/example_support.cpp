#include "example_support.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

using namespace gpu;

#if !defined(__linux__)
DeviceInit create_example_device(void* window) noexcept
{
    if (!window) return {.error = Error::driver_error};
    const DeviceInit result = create_device({.window = window, .swapchain_format = Format::bgra8_srgb});
    if (result.error != Error::none) fprintf(stderr, "Device creation failed: %u\n", unsigned(result.error));
    return result;
}
#endif

Span<byte> read_shader(const char* path) noexcept
{
    FILE* file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    const size_t byte_count = size_t(ftell(file));
    rewind(file);
    Span<byte> code(static_cast<byte*>(malloc(byte_count)), byte_count);
    fread(code.data, 1, code.size, file);
    fclose(file);
    return code;
}

void read_binary_file(const char* path, Span<byte> data) noexcept
{
    FILE* file = fopen(path, "rb");
    assert(file);
    fread(data.data, 1, data.size, file);
    fclose(file);
}
