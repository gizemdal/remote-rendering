# Remote Rendering for XR

![Sneak peek](images/test_3_labeled.png)

![Developer](https://img.shields.io/badge/Developer-Gizem%20Dal-green.svg) ![Developer](https://img.shields.io/badge/Developer-Dayu%20Li-green.svg) ![Developer](https://img.shields.io/badge/Developer-Tushar%20Purang-green.svg)

**Team members:**
- Gizem Dal
  - [Portfolio](https://www.gizemdal.com/), [Linkedin](https://www.linkedin.com/in/gizemdal/)
- Dayu Li
- Tushar Purang

#### Table of Contents  
[Project Description](#description)  
[Setup Overview](#overview)  
[OptiX Ray Tracer](#ray-tracer)
[OBJ & MTL Parsing](#obj-mtl-parsing)
[Resources](#resources)  

<a name="description"/> 

## Project Description

<a name="overview"/>

## Setup Overview

These instructions should help you with running the OptiX Path Tracer sample on Windows.

- Step 1: Download the NVIDIA OptiX 7.2.0 SDK. This will also require a NVIDIA Driver of version 456.71 or newer.
- Step 2: Once you download the SDK, clone our project and cd to optixPathTracer. Create a build directory and cd into it. Run cmake to configure and generate the VS solution.
- Step 3: Once you have the VS solution ready, open it and set optixPathTracer as the start up project. Build the project in Release mode and hit 'Ctrl + F5' to run. You should see an interactive popup window with !Crytek Sponza[https://casual-effects.com/data] rendered with white diffuse material.

<a name="ray-tracer"/>

## OptiX Ray Tracer

We are using the ![NVIDIA OptiX 7.2.0](https://developer.nvidia.com/optix) real-time ray tracing engine for our GPU rendering and benefiting from OptiX acceleration structures to get fast results while rendering scenes with complex meshes.
We downloaded the ![OptiX 7.2.0 samples](https://developer.nvidia.com/designworks/optix/download) for Windows and started our implementation from the optixPathTracer sample. The initial state of the sample had the triangle geometry acceleration structure and the basic ray tracing programs ready; however it only supported hardcoded scene data with single area light and only diffuse material support.
We enhanced this path tracer to handle different types of scene geometry (cube, icosphere and arbitrary meshes), materials (diffuse, perfect specular, imperfect specular, fresnel dielectric) and light sources (area, point and spot light). We also added support for multiple light sources per scene. Another adjustment we made to base implementation is adding Russian Roulette termination for ray paths that have less contribution to the results.

The following render examples use the ![Stanford Bunny](https://casual-effects.com/data/) arbitrary mesh in the scenes. We use the ![tinyobj](https://github.com/tinyobjloader/tinyobjloader) to import arbitary meshes into our scenes. More detail on how we're using this library will be provided in the next section.

Area Lights | Point Lights| Spot Lights
:---: | :---: | :---: 
<img src="images/area_lights.png" alt="Area Lights" width=300> | <img src="images/point_lights.png" alt="sneak peek" width=300> | <img src="images/spot_lights.png" alt="sneak peek" width=300>

<a name="obj-mtl-parsing"/>

## OBJ & MTL Parsing

<a name="resources"/>

## Resources

These resources helped us brainstorm ideas and implement our project. We also included third party libraries that we used for this project.

- ![NVIDIA OptiX 7.2.0 SDK & Samples](https://developer.nvidia.com/optix)
- ![NVIDIA OptiX 7 SIGGRAPH Course Samples by Ingo Wald](https://gitlab.com/ingowald/optix7course)
- ![McGuire Computer Graphics Archive Meshes](https://casual-effects.com/data/)
- ![tinyobjloader](https://github.com/tinyobjloader/tinyobjloader)
- ![Physically Based Rendering: From Theory to Implementation](http://www.pbr-book.org/)
- ![About Azure Remote Rendering](https://docs.microsoft.com/en-us/azure/remote-rendering/overview/about)
- ![High-Quality Real-Time Global Illumination in Augmented Reality](https://www.ims.tuwien.ac.at/projects/rayengine)
- ![A Streaming-Based Solution for Remote Visualization of 3D Graphics on Mobile Devices](https://www.researchgate.net/publication/3411346_A_Streaming-Based_Solution_for_Remote_Visualization_of_3D_Graphics_on_Mobile_Devices)
