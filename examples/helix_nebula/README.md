# Helix Nebula

An asset-free artistic approximation of the [Helix Nebula](https://en.wikipedia.org/wiki/Helix_Nebula),
with a tilted blue-green inner shell, irregular orange-red envelope, radial filaments, and background stars.
The geometry and colors evoke visible-light photographs; this is not a scientific simulation.

A single fullscreen draw integrates emission and absorption through a procedural volume. A shared root
passed by GPU pointer supplies the viewport, gentle viewing motion, and bindless noise texture indices.
The bounded raymarch clips rays to the nebula and evaluates fine structure only inside its gas envelope.
Noise is generated once at startup; rendering needs no external assets or per-frame allocations.

Run `build/examples/helix_nebula/example_helix_nebula`. Close the window to exit, or use `--frames 120`
to render a finite number of frames. The sample uses the repository's normal Metal/Vulkan feature baseline.
