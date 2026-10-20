#pragma once

#include <NoGraphicsAPI/types.h>

#include <stdio.h>

namespace
{

constexpr uint32 profile_history_count = 1024;
constexpr uint32 profile_warmup_frames = 30;
constexpr uint32 cpu_region_count = 6;
constexpr uint32 gpu_region_count = 3;
constexpr const char* cpu_region_names[cpu_region_count] = {"Events", "Frame wait", "Prepare", "Acquire", "Record", "Submit/present"};
constexpr const char* gpu_region_names[gpu_region_count] = {"Simulation", "G-buffer", "Lighting"};

struct ProfileFrame
{
    uint64 number = 0;
    double cpu[cpu_region_count + 1]{};
    uint64 gpu[gpu_region_count + 1]{};
    bool resized = false;
};

struct Profiler
{
    ProfileFrame history[profile_history_count]{};
    double cpu_sum[cpu_region_count]{};
    double gpu_sum[gpu_region_count]{};
    double cpu_peak = 0.0;
    double gpu_peak = 0.0;
    double report_begin = 0.0;
    uint32 sample_count = 0;

    void report() noexcept
    {
        if (!sample_count) return;
        double cpu_total = 0.0;
        double gpu_total = 0.0;
        printf("\n%u frames; average milliseconds (GPU ranges include preceding barriers):\nCPU", sample_count);
        for (uint32 i = 0; i != cpu_region_count; ++i)
        {
            printf("  %s %.3f", cpu_region_names[i], cpu_sum[i] / sample_count);
            cpu_total += cpu_sum[i];
            cpu_sum[i] = 0.0;
        }
        printf("  Total %.3f  Peak %.3f\nGPU", cpu_total / sample_count, cpu_peak);
        for (uint32 i = 0; i != gpu_region_count; ++i)
        {
            printf("  %s %.3f", gpu_region_names[i], gpu_sum[i] / sample_count);
            gpu_total += gpu_sum[i];
            gpu_sum[i] = 0.0;
        }
        printf("  Total %.3f  Peak %.3f\n", gpu_total / sample_count, gpu_peak);
        fflush(stdout);
        sample_count = 0;
        cpu_peak = 0.0;
        gpu_peak = 0.0;
    }

    void complete_frame(const ProfileFrame& frame, float timestamp_period_ns) noexcept
    {
        if (frame.number < profile_warmup_frames || frame.resized) return;
        if (!sample_count) report_begin = frame.cpu[0];
        for (uint32 i = 0; i != cpu_region_count; ++i)
            cpu_sum[i] += (frame.cpu[i + 1] - frame.cpu[i]) * 1000.0;
        for (uint32 i = 0; i != gpu_region_count; ++i)
            gpu_sum[i] += double(frame.gpu[i + 1] - frame.gpu[i]) * double(timestamp_period_ns) * 0.000001;
        if ((frame.cpu[cpu_region_count] - frame.cpu[0]) * 1000.0 > cpu_peak)
            cpu_peak = (frame.cpu[cpu_region_count] - frame.cpu[0]) * 1000.0;
        if (double(frame.gpu[gpu_region_count] - frame.gpu[0]) * double(timestamp_period_ns) * 0.000001 > gpu_peak)
            gpu_peak = double(frame.gpu[gpu_region_count] - frame.gpu[0]) * double(timestamp_period_ns) * 0.000001;
        ++sample_count;
        if (frame.cpu[cpu_region_count] - report_begin >= 1.0) report();
    }

    void write_trace(FILE* file, uint64 frame_count, float timestamp_period_ns) const noexcept
    {
        const uint64 first_frame = frame_count > profile_history_count ? frame_count - profile_history_count : 0;
        const double origin = frame_count ? history[first_frame % profile_history_count].cpu[0] : 0.0;
        fprintf(file, "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[\n"
                      "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":1,\"args\":{\"name\":\"Profiled deferred renderer\"}},\n"
                      "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":1,\"args\":{\"name\":\"CPU frame\"}}");
        for (uint64 number = first_frame; number != frame_count; ++number)
        {
            const ProfileFrame& frame = history[number % profile_history_count];
            fprintf(file, ",\n{\"name\":\"Frame\",\"cat\":\"CPU\",\"ph\":\"X\",\"pid\":1,\"tid\":1,\"ts\":%.3f,\"dur\":%.3f,"
                          "\"args\":{\"frame\":%llu,\"warmup\":%s,\"resized\":%s}}",
                    (frame.cpu[0] - origin) * 1000000.0, (frame.cpu[cpu_region_count] - frame.cpu[0]) * 1000000.0,
                    static_cast<unsigned long long>(number), number < profile_warmup_frames ? "true" : "false", frame.resized ? "true" : "false");
            for (uint32 i = 0; i != cpu_region_count; ++i)
                fprintf(file, ",\n{\"name\":\"%s\",\"cat\":\"CPU\",\"ph\":\"X\",\"pid\":1,\"tid\":1,\"ts\":%.3f,\"dur\":%.3f}",
                        cpu_region_names[i], (frame.cpu[i] - origin) * 1000000.0, (frame.cpu[i + 1] - frame.cpu[i]) * 1000000.0);

            // Counter samples belong to this CPU submission, not to an uncalibrated GPU execution timeline.
            fprintf(file, ",\n{\"name\":\"GPU pass durations (ms)\",\"ph\":\"C\",\"pid\":1,\"ts\":%.3f,\"args\":{",
                    (frame.cpu[cpu_region_count] - origin) * 1000000.0);
            for (uint32 i = 0; i != gpu_region_count; ++i)
                fprintf(file, "\"%s\":%.6f,", gpu_region_names[i],
                        double(frame.gpu[i + 1] - frame.gpu[i]) * double(timestamp_period_ns) * 0.000001);
            fprintf(file, "\"Total\":%.6f}}", double(frame.gpu[gpu_region_count] - frame.gpu[0]) * double(timestamp_period_ns) * 0.000001);
        }
        fprintf(file, "\n]}\n");
    }
};

} // namespace
