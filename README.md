# Remote Rendering for XR

![Sneak peek](images/readme_main_labeled.png)
*The dragon and sponza meshes are downloaded from [McGuire Computer Graphics Archive Meshes](https://casual-effects.com/data/)*

![Developer](https://img.shields.io/badge/Developer-Gizem%20Dal-green.svg) ![Developer](https://img.shields.io/badge/Developer-Dayu%20Li-green.svg) ![Developer](https://img.shields.io/badge/Developer-Tushar%20Purang-green.svg)

**Team members:**
- Gizem Dal
  - [Portfolio](https://www.gizemdal.com/), [Linkedin](https://www.linkedin.com/in/gizemdal/)
- Dayu Li
  - [Linkedin](https://www.linkedin.com/in/dayu95/)
- Tushar Purang
  - [Portfolio](https://tushvr.com/), [Linkedin](https://www.linkedin.com/in/tpurang/)

#### Table of Contents  
[Project Description](#description)  
[Setup Overview](#overview)  
[OptiX Ray Tracer](#ray-tracer)   
[OBJ & MTL Parsing](#obj-mtl-parsing)   
[Streaming & Network](#streaming)   
[Performance Analysis](#performance)  
[Resources](#resources)  

<a name="description"/> 

## Project Description

The goal of our project is to use the power of GPU rendering to get real-time path tracing results of scenes, pass the render frames from device to host memory to make it accessible by a server Unity Desktop Application and then send these frames to be viewed through XR platforms such as Microsoft Hololens or Android devices. Real time ray tracing on mixed reality platforms is still an open research problem today and we're very excited to have this opportunity to experiment with this field and share our results.

In order to achieve real-time path tracing on the GPU, we're using the [NVIDIA OptiX Ray Tracing Engine (Version 7.2.0)](https://developer.nvidia.com/optix) which is designed to accelerate ray tracing applications on NVIDIA GPUs and allow users to program intersection, ray generation and shading components.

<img src="images/diagram.jpg" alt="Diagram" width=800>

<a name="overview"/>

## Setup Overview

These instructions should help you with running the OptiX Path Tracer sample on Windows.
- Step 1: Download the NVIDIA OptiX 7.2.0 SDK. This will also require a NVIDIA Driver of version 456.71 or newer.
- Step 2: Once you download the OptiX SDK, clone our project and cd to the optix_sdk_7_2_0/SDK folder.
- Step 3: Create a new folder named 'data' and create a subfolder named 'Sponza' inside. Download the [Crytek Sponza](https://casual-effects.com/data/) mesh and put all the files inside the new Sponza folder.
- Step 4: Go back to the SDK parent folder. Create a build directory and cd into it. Run cmake to configure and generate the VS solution.
	- You may have to set the GLM directory manually while configuring the project. GLM headers are included in ```optix_sdk_7_2_0/SDK/support/glm```.
- Step 5: Once you have the VS solution ready, open it and set optixPathTracer as the start up project.
- Step 6: Set build mode to Release and open Properties->Debugging. Set ```--scene ../../../../scenes/scene_example.txt``` as the command line argument. We use the --scene flag to let the program know that we're passing a scene file path.
	- You may also test the simple Cornell Box scene at ```../../../../scenes/basic_cornell.txt```
- Step 7: Build the project in Release mode and hit 'Ctrl + F5' to run. If you have loaded scene_example.txt, you should see an interactive popup window with Sponza rendered with different colors per material.
	- Use the left mouse button to change camera orientation, middle mouse button to zoom in/out and right mouse button to pan the camera.
	- Press 'S' on the keyboard to save the current render frame. The render frame will be saved as 'output.png' under SDK/build/optixPathTracer. This file will be overwritten by the next saved frame unless you change its name!

<a name="ray-tracer"/>

## OptiX Ray Tracer

We are using the [NVIDIA OptiX 7.2.0](https://developer.nvidia.com/optix) real-time ray tracing engine for our GPU rendering and benefiting from OptiX acceleration structures to get fast results while rendering scenes with complex meshes.
We downloaded the [OptiX 7.2.0 samples](https://developer.nvidia.com/designworks/optix/download) for Windows and started our implementation from the optixPathTracer sample. The initial state of the sample had the triangle geometry acceleration structure and the basic ray tracing programs ready; however it only supported hardcoded scene data with single area light and only diffuse material support.
We enhanced this path tracer to handle different types of scene geometry (cube, icosphere and arbitrary meshes), materials (diffuse, perfect specular, imperfect specular, fresnel dielectric) and light sources (area, point and spot light). We also added support for multiple light sources per scene. Another adjustment we made to base implementation is adding Russian Roulette termination for ray paths that have less contribution to the results.

The following render examples use the [Stanford Bunny](https://casual-effects.com/data/) arbitrary mesh in the scenes. We use the [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) to import arbitary meshes into our scenes. More detail on how we're using this library will be provided in the next section.

Area Lights | Point Lights| Spot Lights
:---: | :---: | :---: 
<img src="images/area_lights.png" alt="Area Lights" width=300> | <img src="images/point_lights.png" alt="sneak peek" width=300> | <img src="images/spot_lights.png" alt="sneak peek" width=300>

#### Scene Files

Scene files are passed as command line arguments with the "--scene" flag at front. For example; you can load the scene_example.txt test scene in this repository with the following command line argument: ```--scene ../../../../scene_example.txt```

There are 3 types of scene objects that you can add to the scene: CAMERA, MATERIAL and GEOMETRY. Every scene file must add a camera, if there are multiple camera items in the file only the first camera will be set as scene camera. All materials must be added before geometry!

Every material add must follow this argument pattern: ```MATERIAL (material type) (diffuse color) (specular color) (emissive color) (specular exponent) (ior)```

Every geometry add must follow this argument pattern: ```GEOMETRY (geometry type) (material id - this is the order the material is added, ordering starts from id 0) (translate vector) (rotate vector) (scale vector) (obj filepath)```

Every camera add must follow this argument pattern: ```CAMERA (render width) (render height) (eye vector) (lookat vector) (up vector) (fovy)```

<a name="obj-mtl-parsing"/>

## Import Meshes & Texture Mapping

<img src="images/shield.gif" alt="sheild" width=800>

To achieve our goal of rendering assets and objects remotely, the ray tracer should have the ability to render arbitrary meshes and import external mesh files. We used a 3rd party tool called [tiny_obj loader](https://github.com/tinyobjloader/tinyobjloader) to achieve the import of external .obj files as multiple triangles. The imported mesh can be set as any material types saved in the pathtracer. Furthermore, we would like to let the Hololens users see not only meshes in simple materials but also objects that looks real. Thus we added the features of texture mapping. The mtl loader takes the .mtl files that are usually provided along with the .obj files, it reads the pieces with same material types and let the path tracer render them as independent pieces. Then the texture loader will load the texture images and transfer them into texture objects in Optix.

The following images shows how the imported obj mesh looks like with only obj, obj and mtl, and obj,mtl,textures.

| Obj Loader | Mtl Loader | Texture Loader| 
| :----------------------------------------------------------: | :----------------------------------------------------------: | :----------------------------------------------------------: |
| <img src="images/sponza.gif" alt="OBJ" width=300> | <img src="images/sponza with mtl.gif" alt="MTL" width=300> | <img src="images/texture.gif" alt="TXT" width=300> |

*The sponza mesh and textures are downloaded from [McGuire Computer Graphics Archive Meshes](https://casual-effects.com/data/), shield mesh and textures are downloaded from [Turbo Squid](https://www.turbosquid.com/Search/3D-Models/free/textured)*

<a name="streaming"/>

## Streaming & Network

For each frame cycle, the frame buffer is dumped into an image file on device memory. This frame is read by the desktop server application and sent to the HoloLens 2 application. Networking is done using Unity's UNet. 

<img src="images/diagramNetworking.png" alt="Area Lights" width=500>



| Milestone 1: Server-Client Frame Streaming <br />(Running on same Machine) | Milestone 2: Raytracer to Android Frame streaming<br />(Running on different Machines) | Milestone 3: Desktop Server to Hololens 2                    |
| :----------------------------------------------------------: | :----------------------------------------------------------: | ------------------------------------------------------------ |
| <img src="images/streaming.gif" alt="Area Lights" width=300> | <img src="images/streaming2.gif" alt="Area Lights" width=300> | <img src="images/streaming3.gif" alt="Area Lights" width=300> |


## Camera Synchronization

Based on the file I/O system, the raytracer can read data from external files, thus provide supports for camera synchronization. Provided with the path of data file, the ray tracer can keep track of the oridentation of it's main camera in each frame and synchronize it with the data. This feature can cooperate with any forms of data recorder which outputs the data in each frame to realize the camera synchronization.

<img src="images/camera.gif" alt="Camera Sync" width=800>

<a name="performance"/>

## Performance Analysis
### Latency Analysis

<img src="images/latency.jpeg" alt="Latency" width=800>

The dataflow diagram above shows the sources of the latency. In each interation, first the state will be updated to create a subframe, then the subframe will be loaded from device to host and exported as a image file. Then the image file will be imported into the Unity server app as bytes and transfered as a Unity 2D texture object. Finally the texutre will be transmitted through Wifi to the client's end and being displayed. The four blocks showed above are the major latency sources. To reduce the latency we made several attempts to optimize our pipe line and here is the current latencies.

| Step | Time |
|---|---|
| Generate Subframe | 7.28 ms|
| Save Image | 4.2 ms|
| Load Image in Server | ? ms|
| Wifi Transmission | ? ms|

*Tested with the sample dragon scene shown above with 768 of image size and 4 samples per subframe, depth is 3*

<img src="images/dragon.png" alt="dragon scene" width=400>

### Optimization Attempts

#### 4-Way Image Split

**Important: The performance analysis of this section is ran on a Intel(R) Core(TM) i7-7700HQ CPU @ 2.80 GHz 2.81 GHz with NVIDIA GeForce GTX 1060 graphics card. This analysis should serve as a comparison between different parameters rather than a performance benchmark, since runtimes will depend on the machine.**

As an attempt to reduce image loading times in the server, we tested splitting the output/frame buffer into 4 smaller buffers and export the frame PPM image in 4 smaller parts. When we say that we split the buffer into 4 smaller buffers, this does not mean that we're creating 4 buffers that is quarter the size of our original buffer and copying original buffer memory into each of them. We are achieving our buffer split by creating another single buffer that is quarter the size of our original buffer and moving its data pointer to point at the corresponding original buffer memory by getting the host pointer of the original buffer at each quarter image save. Since we're optimizing our code with ZERO_COPY (check Zero Copy optimization section for more detail), getting the host pointer of the original buffer does not result in a device to host memcpy operation.

Saving a PPM image 4 times instead of 1 results in slower save image times on the path tracer side. The results below are recorded with the basic Cornell box scene file we provided in our repository and they do not use color compression (check Color Compression optimization section for more detail).

<img src="images/split_graph.png" alt="4-way vs full chart" width=650>

#### Color Compression

**Important: The performance analysis of this section is ran on a Intel(R) Core(TM) i7-7700HQ CPU @ 2.80 GHz 2.81 GHz with NVIDIA GeForce GTX 1060 graphics card. This analysis should serve as a comparison between different parameters rather than a performance benchmark, since runtimes will depend on the machine.**

In order to save the resulting output buffer at each subframe we call the saveImage() function provided by the OptiX sutil library which supports exporting images in both PNG and PPM format. We're currently exporting PPM images rather than PNG due to significanly reduced file sizes.

We originally had the output/frame buffer support accumulated color data of RGBA8 (32 bits total) per ray path and ignore the alpha component when it comes to writing the image data into pixels. In the hopes of reducing the time it takes to export a single frame, we searched ways of reducing the memory needed to store color information. We updated our output buffer to store color data in [RGB565 compressed format](http://www.barth-dev.de/online/rgb565-color-picker/), which would use 16 bits total per ray path, and then decompress the RGB565 color data into RGB8 while writing the image data into pixels since the PPM image writer by ostream expects 8 bits per channel. We also updated the sutil imageSave() function to support image data of UNSIGNED_BYTE2.

We tested the results of color compression with our basic Cornell box scene file and recorded the time it takes saveImage() to perform at each subframe in the render loop. We compared these results to the time data we collected from uncompressed subframes and put both results together in a chart to analyze. We measured the times in milliseconds for the 20 first subframes.

![Export Chart](images/color_compression.png)

As seen in the chart above, saving uncompressed image data results in a lot of fluctuation while compressed image data maintains a steadier runtime. We also measured the render and display times at each 20 subframe. Render time corresponds to how long it takes to trace all ray paths with the given number of samples per subframe and depth. Display time corresponds to how long it takes to create a GL 2D texture from the frame buffer and update the display on the screen.

<img src="images/render_time.png" alt="Render Time Chart" width=650> <img src="images/display_time.png" alt="Display Time Chart" width=650>

Using compressed vs uncompressed colors do not have a significant impact on the time it takes for all ray paths to be traced with the given parameters. The display update with uncompressed colors is slightly faster compared to using compressed colors, however this would no longer have an impact if we reserve showing each frame to the Hololens rather than show them simultaneously on the desktop. The graph below shows the recorded FPS for uncompressed frames without display and compressed frames with and without display.

<img src="images/fps_graph.png" alt="FPS Chart" width=650>

We see a slight increase in FPS for compressed frames when the display is disabled. The FPS rates for compressed images overall are more uniform compared to those of uncompressed frames due to more stable frame image save times.

Although compressed frames have more uniform frame rates, we can observe slight color artifacts because we're storing less precise color information. This is more noticeable with renders without any camera movement, thus the frame undergoes more samples and becomes more converged. However, since our aim is using these render frames for platforms with frequent camera movement such as Hololens, we believe that the slight loss of image quality is a reasonable tradeoff.

| Uncompressed | Compressed
| :----------------------------------------------------------: | :----------------------------------------------------------:
<img src="images/ucomp.png" alt="Uncompressed" width=450> | <img src="images/comp.png" alt="Compressed" width=450>

<a name="resources"/>

## Resources

These resources helped us brainstorm ideas and implement our project. We also included third party libraries that we used for this project.

- [NVIDIA OptiX 7.2.0 SDK & Samples](https://developer.nvidia.com/optix)
- [NVIDIA OptiX 7 SIGGRAPH Course Samples by Ingo Wald](https://gitlab.com/ingowald/optix7course)
- [McGuire Computer Graphics Archive Meshes](https://casual-effects.com/data/)
- [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader)
- [Physically Based Rendering: From Theory to Implementation Online Textbook](http://www.pbr-book.org/)
- [About Azure Remote Rendering](https://docs.microsoft.com/en-us/azure/remote-rendering/overview/about)
- [High-Quality Real-Time Global Illumination in Augmented Reality](https://www.ims.tuwien.ac.at/projects/rayengine)
- [A Streaming-Based Solution for Remote Visualization of 3D Graphics on Mobile Devices](https://www.researchgate.net/publication/3411346_A_Streaming-Based_Solution_for_Remote_Visualization_of_3D_Graphics_on_Mobile_Devices)
