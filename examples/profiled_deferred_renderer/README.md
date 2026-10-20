# Profiled deferred renderer

The advanced deferred scene with 1,048,576 animated cubes, CPU timing summaries, and GPU timestamps for simulation, G-buffer rendering, and lighting.
Requires GPU timestamp support. No additional dependencies.

Run an optimized build from a terminal. Summaries report mean region times and peak frame times every second, excluding the first 30 frames and resize frames.
CPU work, frame-slot waits, image acquisition, and submission/presentation are measured separately. CPU preparation includes timestamp collection and reporting.
GPU ranges include their preceding barriers; GPU total covers simulation through lighting, excluding acquisition, presentation, and resize initialization.
GPU results are collected after the existing frame-slot wait, before the command pool is reset, without adding waits.

```sh
./build-native-wayland/examples/profiled_deferred_renderer/example_profiled_deferred_renderer --trace profile.json --frames 600
```

Both arguments are optional. Without `--frames`, close the window to finish. `--trace` exports the latest 1,024 submitted frames on normal exit,
including warmup and resize frames, to [Perfetto](https://ui.perfetto.dev). CPU regions appear as timeline ranges; GPU durations appear as counter graphs
associated with each CPU submission. These counters do not locate GPU execution on the CPU timeline because the clocks are not calibrated.
Trace output is buffered in fixed CPU storage and written after the GPU drains.

For shader hotspots, occupancy, and memory bottlenecks, launch the optimized executable through NVIDIA Nsight Graphics and use GPU Trace.
Use Nsight Systems for CPU/driver/submission correlation. Profiling adds overhead; compare at the same drawable resolution with validation disabled.
