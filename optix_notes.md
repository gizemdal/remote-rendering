OptiX 7.2.0 Path Tracer Sample Notes
================

**Files:**
- optixPathTracer.cpp
- optixPathTracer.h
- optixPathTracer.cu
- Dependencies: optix.h (OptiX public API header)

[NVIDIA OptiX Programming Guide](https://raytracing-docs.nvidia.com/optix7/guide/index.html#preface#)

[NVIDIA OptiX API](https://raytracing-docs.nvidia.com/optix7/api/html/modules.html)

## Programming Guide Summary ##

The following notes are summaries taken from the online programming guide.

**Program:** Block of executable code on GPU that represents a particular shading operation. These are programmable components.

**Types of programs:**
- Ray generation: entry point into ray tracing pipeline, run in parallel for each pixel
- Intersection: implements ray-primitive intersection test
- Any-hit: called when a traced ray finds a new intersection point
- Closest-hit: called when a traced ray finds the closest intersection point
- Miss: called when a traced ray misses all scene geometry
- Exception: exception handler
- Direct callables: similar to CUDA function call, called immediately
- Continuation callables: executed by the scheduler

**Shader binding table:** connects geometric data to programs and their parameters.

**Record:** component of the shader binding table that is selected during execution by using offsets specified when acceleration structures are created and at runtime. Made of header and data (user data associated with the primitive or programs referenced in the headers can be stored here)

**Acceleration structures:** built on device, typically based on the bounding volume hierarchy model. OptiX provides two types.

**Geometry acceleration structure:** built over primitives (triangles, curves or user defined primitives)

**Instance acceleration structure:** built over other objects such as acceleration structures or motion transform nodes

# optixPathTracer.cpp: ##

You will see a bunch of variables that uses "gas" as a prefix - GAS stands for geometry acceleration structure. IAS is for instance acceleration structure.

Stores camera state variables (Line 67)

Stores mouse state variables (Line 72)

**PathTracerState:** struct for storing path tracer info, I believe the most important component for us is params (of type Params - check optixPathTracer.h for struct details)

Scene data starts at line 146 - for this scene they're triangulating the geometry and passing the vertex buffer entries manually but we could implement a function for this

**Vertex:** made of x, y, z and pad

**TRIANGLE_COUNT:** number of triangles scene data contains

**MAT_COUNT:** material count

**g_mat_indices:** buffer that matches every triangle to their material index

**g_emission_colors:** emission buffer (indices must match material indices), only light sources have non-zero emission

**g_diffuse_colors:** buffer for diffuse color of each material (indices must match material indices)

This scene currently handles diffuse materials only, if we want to handle more materials we will need to add more buffers (specular_color, ior, etc.)

**GLFW callbacks** start at line 325 - these callbacks handle mouse, key, windows and cursor interactions

**initLaunchParams (Line 426):** sets the state (of type PathTracerState). Current sample implementation supports one area light source per scene, we will have to change this to support multiple light sources

**displaySubframe (Line 516):** calls display() to display the current render results, calls getPBO() on the output_buffer (of type CUDAOutputBuffer<uchar4>)

**buildMeshAccel (Line 574):** builds the acceleration structure for ray-scene intersections. Calls optixAccelBuild() (check the API documentation for more info on the parameters). numBuildInputs is set to 1 (supports both GAS and IAS) Seems like we don't have to change anything about this function for now

**createModule (Line 685):** current path tracer state doesn't use motion blur, it currently uses a single GAS

createProgramGroups, createPipeline, createSBT, cleanupState: handles the setups and memory allocations for program groups and buffers.

**main() steps:**
- First parse the command line arguments, we're currently not passing anything
  - "--file" or "-f": pass an output filename - if the output file name is not set then we will view the render results on screen until we exit the window
  - "--dim=": set output dimensions
  - "--launch-samples" or "-s": set number of samples
  
- Camera and OptiX states are initialized
- If an output file is not provided as a command line argument we will display the interactive render window. We create the output buffer. launchSubframe() is called to start the ray tracing process and update the render results with more samples at each frame - this function will be called at every iteration until we exit the window
- If an output file is provided, we won't see the render window. launchSubframe() will be called once and the render result will be saved to the output file. Once this is done the program will exit automatically.
