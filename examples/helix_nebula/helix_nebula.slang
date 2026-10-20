#include <NoGraphicsAPI/shader.slang>
#include "helix_nebula_shared.h"

GPU_ROOT(HelixNebulaRoot, root);

struct FullscreenVertex
{
    float4 position : SV_Position;
};

[shader("vertex")]
FullscreenVertex vertexMain(uint vertex_index : SV_VertexID)
{
    static const float2 positions[3] = {float2(-1.0f, -1.0f), float2(3.0f, -1.0f), float2(-1.0f, 3.0f)};
    FullscreenVertex output;
    output.position = float4(positions[vertex_index], 0.0f, 1.0f);
    return output;
}

float3 hash3(float2 position)
{
    float3 value = frac(float3(position.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    value += dot(value, value.yxz + 33.33f);
    return frac((value.xxy + value.yzz) * value.zyx);
}

float3 stars(float2 position, float scale, float threshold, float brightness)
{
    float3 random = hash3(floor(position * scale));
    if (random.z < threshold) return 0.0f;
    float2 offset = frac(position * scale) - (0.15f + 0.7f * random.xy);
    float width = max(0.018f + 0.024f * random.z, root.inverse_extent.y * scale * 1.9f);
    float core = exp2(-dot(offset, offset) / (width * width));
    float halo = exp2(-length(offset) * 17.0f) * 0.08f;
    float3 color = lerp(float3(0.55f, 0.72f, 1.0f), float3(1.0f, 0.7f, 0.44f), random.x);
    return color * (core + halo) * brightness * (1.0f - smoothstep(0.08f, 0.15f, length(offset)));
}

float3 nebula_position(float3 position)
{
    float2 rotated = float2(root.orientation.z * position.x + root.orientation.w * position.y,
                            -root.orientation.w * position.x + root.orientation.z * position.y);
    return float3(rotated.x, root.orientation.x * rotated.y + root.orientation.y * position.z,
                  -root.orientation.y * rotated.y + root.orientation.x * position.z);
}

float gaussian(float value)
{
    return exp2(-value * value * 1.442695f);
}

float4 gas(float3 position, Texture3D<float4> noise, SamplerState filtering)
{
    float radius = length(position.xy);
    if (radius > 1.57f || abs(position.z) > 0.77f) return 0.0f;

    float4 coarse = noise.SampleLevel(filtering, position * 0.067f + float3(0.17f, 0.31f, 0.53f), 0.0f);
    float3 warped = position + (coarse.xyz - 0.5f) * 0.16f;
    float4 cloud = noise.SampleLevel(filtering, warped * 0.24f + float3(0.43f, 0.11f, 0.27f), 0.0f);
    float3 direction = float3(position.xy / max(radius, 0.01f), position.z);
    float asymmetry = 0.045f * direction.x - 0.035f * direction.y + 0.055f * direction.x * direction.y;
    float shell_radius = radius + (coarse.w - 0.5f) * 0.18f + (cloud.y - 0.5f) * 0.07f + asymmetry;
    float plane = position.z + 0.055f * direction.x - (coarse.y - 0.5f) * 0.12f;

    float envelope = gaussian((shell_radius - 1.08f) / 0.23f) * gaussian(plane / 0.29f);
    float oxygen = gaussian((shell_radius - 0.79f) / 0.23f) * gaussian(plane / 0.24f);
    float inner = gaussian(radius / 0.7f) * gaussian(position.z / 0.36f) * 0.095f;
    float halo = gaussian((shell_radius - 1.36f) / 0.13f) * gaussian((plane + position.x * 0.22f) / 0.19f) * 0.16f;
    if (envelope + oxygen + inner + halo < 0.008f) return 0.0f;

    float4 detail = noise.SampleLevel(filtering, warped * 0.73f + float3(0.21f, 0.63f, 0.07f), 0.0f);
    float4 radial = noise.SampleLevel(filtering, float3(direction.xy * 0.64f + plane * 0.035f, radius * 0.045f + plane * 0.12f), 0.0f);
    float cloud_density = max(0.0f, cloud.x * 0.7f + detail.x * 0.3f - 0.24f) * 2.1f;
    float filaments = pow(saturate(radial.x * 1.35f), 4.0f);
    float knots = gaussian((shell_radius - 0.91f - radial.y * 0.12f) / 0.085f) * gaussian(plane / 0.21f);
    float dust = knots * pow(saturate(radial.z * 1.7f - 0.5f), 3.0f) * 0.9f;
    float gas_density = (envelope * (0.4f + filaments * 1.6f) + oxygen * 0.65f) * cloud_density;

    float3 ion_color = lerp(float3(0.055f, 0.23f, 0.66f), float3(0.12f, 0.85f, 1.0f), smoothstep(0.45f, 0.85f, shell_radius));
    ion_color = lerp(ion_color, float3(1.05f, 0.9f, 0.56f), smoothstep(0.82f, 0.98f, shell_radius));
    ion_color = lerp(ion_color, float3(1.5f, 0.32f, 0.055f), smoothstep(0.98f, 1.15f, shell_radius));
    ion_color = lerp(ion_color, float3(0.7f, 0.055f, 0.018f), smoothstep(1.14f, 1.4f, shell_radius));
    float3 emission = ion_color * gas_density * 3.3f;
    emission += float3(0.07f, 0.19f, 0.53f) * inner * (0.55f + coarse.z) * 2.2f;
    emission += float3(0.95f, 0.065f, 0.025f) * halo * cloud_density;
    emission += float3(1.1f, 0.58f, 0.16f) * knots * filaments * detail.y * 0.35f;
    float boundary_fade = (1.0f - smoothstep(1.4f, 1.57f, radius)) * (1.0f - smoothstep(0.58f, 0.77f, abs(position.z)));
    return float4(emission, gas_density * 0.42f + dust * 4.0f + halo * 0.2f) * boundary_fade;
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertex input) : SV_Target0
{
    float2 screen = (input.position.xy * root.inverse_extent * 2.0f - 1.0f) * float2(root.aspect, -1.0f) * root.view_scale;
    float3 background = float3(0.0010f, 0.0015f, 0.0030f);
    background += stars(screen + float2(3.1f, 1.7f), 33.0f, 0.985f, 0.7f);
    background += stars(screen + float2(1.9f, 4.3f), 14.0f, 0.975f, 1.5f);
    background += stars(screen + float2(7.3f, 2.4f), 6.0f, 0.976f, 4.5f);

    float star_distance = length(screen - float2(0.012f, -0.018f));
    float star_width = max(root.inverse_extent.y * 1.6f, 0.0015f);
    background += float3(0.72f, 0.85f, 1.0f) * (exp2(-star_distance * star_distance / (star_width * star_width)) * 7.0f
                   + exp2(-star_distance * 155.0f) * 0.15f);

    float3 radiance = 0.0f;
    float transmittance = 1.0f;
    float ray_length_squared = 1.8f * 1.8f - dot(screen, screen);
    if (ray_length_squared > 0.0f)
    {
        Texture3D<float4> noise = gpu_texture<Texture3D<float4>>(root.noise_texture);
        SamplerState filtering = gpu_sampler(root.noise_sampler);
        float half_length = sqrt(ray_length_squared);
        float step_size = half_length * (2.0f / 88.0f);
        float jitter = hash3(input.position.xy).x;
        float3 origin = nebula_position(float3(screen, half_length - jitter * step_size));
        float3 step = nebula_position(float3(0.0f, 0.0f, -step_size));
        [loop]
        for (uint i = 0; i != 88; ++i)
        {
            float4 sample = gas(origin + step * float(i), noise, filtering);
            radiance += transmittance * sample.rgb * step_size;
            transmittance *= exp2(-sample.a * step_size * 1.442695f);
        }
    }
    float3 color = radiance + background * transmittance;
    color = 1.0f - exp2(-color * 1.18f);
    return float4(color, 1.0f);
}
