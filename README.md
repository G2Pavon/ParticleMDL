# ParticleMDL

Simulate particles using animated models

<img width="2083" height="1308" alt="image" src="https://github.com/user-attachments/assets/bb2cfa86-8ac4-437a-b066-850209331f3e" />


# Build
The libraries under `third_party/` (Dear ImGui, nlohmann/json, stb and
tinyfiledialogs) are included in the repository, so they need no download.
GLFW is the one system dependency.

- A C++20 compiler.
- CMake 3.24 or newer.
- GLFW 3.3 or newer.
- OpenGL 3.3 headers and the windowing-system headers that GLFW builds against.

```bash
cmake -S . -B build
cmake --build build
```
