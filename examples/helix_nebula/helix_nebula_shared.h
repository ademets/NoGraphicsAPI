#pragma once

#include <NoGraphicsAPIUtility/shader_types.h>

struct HelixNebulaRoot
{
    float2 inverse_extent;
    float aspect;
    float view_scale;
    float4 orientation;
    uint32 noise_texture;
    uint32 noise_sampler;
    uint32 root_alignment_padding0[2];
};
