//
// Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include <glad/glad.h>  // Needs to be included before gl_interop

#include <cuda_gl_interop.h>
#include <cuda_runtime.h>

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

#include <optix.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <sampleConfig.h>

#include <sutil/CUDAOutputBuffer.h>
#include <sutil/Camera.h>
#include <sutil/Exception.h>
#include <sutil/GLDisplay.h>
#include <sutil/Matrix.h>
#include <sutil/Trackball.h>
#include <sutil/sutil.h>
#include <sutil/vec_math.h>
#include <optix_stack_size.h>

#include <GLFW/glfw3.h>
#include "optixPathTracer.h"
#include "tiny_obj_loader.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <set>
#include <vector>
#define TINYOBJLOADER_IMPLEMENTATION 

bool resize_dirty = false;
bool minimized    = false;
bool saveRequested = false;
bool re_render = true;

// Camera state
bool             camera_changed = true;
sutil::Camera    camera;
sutil::Trackball trackball;

// Mouse state
int32_t mouse_button = -1;

int32_t samples_per_launch = 4;

int depth = 6;

//------------------------------------------------------------------------------
//
// Local types
// TODO: some of these should move to sutil or optix util header
//
//------------------------------------------------------------------------------

template <typename T>
struct Record
{
    __align__( OPTIX_SBT_RECORD_ALIGNMENT ) char header[OPTIX_SBT_RECORD_HEADER_SIZE];
    T data;
};

typedef Record<RayGenData>   RayGenRecord;
typedef Record<MissData>     MissRecord;
typedef Record<HitGroupData> HitGroupRecord;


struct Vertex
{
    float x, y, z, pad;
};


struct IndexedTriangle
{
    uint32_t v1, v2, v3, pad;
};


struct Instance
{
    float transform[12];
};

struct Triangle {
    glm::vec3 vertex[3];     // Vertices
    glm::vec3 normal;        // Normal

    //Material data;
    glm::vec3 diffuse;
};

struct Mesh {
    std::vector<Triangle*> triangles;
    glm::vec3 diffuse;
};

struct Model {
    ~Model()
    {
        for (auto mesh : meshes) delete mesh;
    }
    bool material;
    std::vector<Mesh*> meshes;
};

struct PathTracerState
{
    OptixDeviceContext context = 0;

    OptixTraversableHandle         gas_handle               = 0;  // Traversable handle for triangle AS
    CUdeviceptr                    d_gas_output_buffer      = 0;  // Triangle AS memory
    CUdeviceptr                    d_vertices               = 0;
    CUdeviceptr                    d_lights                 = 0;

    OptixModule                    ptx_module               = 0;
    OptixPipelineCompileOptions    pipeline_compile_options = {};
    OptixPipeline                  pipeline                 = 0;

    OptixProgramGroup              raygen_prog_group        = 0;
    OptixProgramGroup              radiance_miss_group      = 0;
    OptixProgramGroup              occlusion_miss_group     = 0;
    OptixProgramGroup              radiance_hit_group       = 0;
    OptixProgramGroup              occlusion_hit_group      = 0;

    CUstream                       stream                   = 0;
    Params                         params;
    Params*                        d_params;

    OptixShaderBindingTable        sbt                      = {};
};


//------------------------------------------------------------------------------
//
// Scene data
//
//------------------------------------------------------------------------------
// Buffers - These are initially dynamic
int32_t TRIANGLE_COUNT = 0;
int32_t MAT_COUNT = 0;

std::vector<Vertex> d_vertices;
std::vector<Material> d_mat_types;
std::vector<uint32_t> d_material_indices;
std::vector<float3> d_emission_colors;
std::vector<float3> d_diffuse_colors;
std::vector<float3> d_spec_colors;
std::vector<float> d_spec_exp;
std::vector<float> d_ior;
std::vector<Triangle> d_triangles;
std::vector<Light> d_lights;

static Vertex toVertex(glm::vec3& v, glm::mat4& t)
{
    // transform the v
    v = glm::vec3(t * glm::vec4(v, 1.f));
    return { v.x, v.y, v.z, 0.f };
}

static glm::vec3 randomColor(int i)
{
    {
        int r = unsigned(i) * 13 * 17 + 0x234235;
        int g = unsigned(i) * 7 * 3 * 5 + 0x773477;
        int b = unsigned(i) * 11 * 19 + 0x223766;
        return glm::vec3((r & 255) / 255.f,
            (g & 255) / 255.f,
            (b & 255) / 255.f);
    }
}

static Vertex fixToUnitSphere(Vertex v) 
{
    // fix vertex position to be on unit sphere
    float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    return { v.x / length, v.y / length, v.z / length, 0.f };
}

static int addMaterial(Material m,
                        float3 dif_col,
                        float3 spec_col,
                        float3 em_col,
                        float spec_exp,
                        float ior)
{
    d_mat_types.push_back(m);
    d_diffuse_colors.push_back(dif_col);
    d_spec_colors.push_back(spec_col);
    d_emission_colors.push_back(em_col);
    d_spec_exp.push_back(spec_exp);
    d_ior.push_back(ior);
    MAT_COUNT++;
    return MAT_COUNT - 1;
}

// Reference: TinyOBJ Sample code: https://github.com/tinyobjloader/tinyobjloader
Model* loadMesh(std::string filename) {

    const std::string mtlDir
        = filename.substr(0, filename.rfind('/') + 1);
    Model* model = new Model;
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;
    bool material = false;
    // load obj
    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str(), mtlDir.c_str());

    if (!warn.empty()) {
        std::cout << warn << std::endl;
    }

    if (!err.empty()) {
        std::cerr << err << std::endl;
    }

    if (!ret) {
        exit(1);
    }

    if (!materials.empty())
    {
        material = true;
        std::cout << "mtl file loaded!" << std::endl;
    }
    // Loop over shapes
    for (size_t s = 0; s < shapes.size(); s++) {
        // Loop over faces(polygon)

        if (material)
        {
            model->material = true;
            tinyobj::shape_t& shape = shapes[s];
            size_t index_offset = 0;
            std::set<int> materialIDs;
            for (auto faceMatID : shape.mesh.material_ids)
            {
                materialIDs.insert(faceMatID);
            }

            for (int materialID : materialIDs) {
                Mesh* mesh = new Mesh;

                for (int f = 0; f < shape.mesh.material_ids.size(); f++) {
                    if (shape.mesh.material_ids[f] != materialID) continue;
                    Triangle* t = new Triangle;
                    int fv = shape.mesh.num_face_vertices[f];

                    for (size_t v = 0; v < fv; v++) {
                        // access to vertex
                        // Here only indices and vertices are useful
                        tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
                        tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
                        tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
                        tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];

                        t->vertex[v] = glm::vec3(vx, vy, vz);
                    }
                    index_offset += fv;

                    // Compute the initial normal using glm::normalize
                    t->normal = glm::normalize(glm::cross(t->vertex[1] - t->vertex[0], t->vertex[2] - t->vertex[0]));
                    //t->diffuse = (const vec3f&)materials[materialID].diffuse;
                    mesh->triangles.push_back(t);
                }
                mesh->diffuse = randomColor(materialID);
                if (mesh->triangles.empty())
                    delete mesh;
                else
                    model->meshes.push_back(mesh);
            }
        }
        if (!material) {
            model->material = false;
            Mesh* mesh = new Mesh;
            size_t index_offset = 0;
            for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {
                int fv = shapes[s].mesh.num_face_vertices[f];
                // Loop over vertices in the face.
                Triangle* t = new Triangle;

                for (size_t v = 0; v < fv; v++) {
                    // access to vertex
                    // Here only indices and vertices are useful
                    tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
                    tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
                    tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
                    tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];

                    t->vertex[v] = glm::vec3(vx, vy, vz);
                }

                index_offset += fv;

                // Compute the initial normal using glm::normalize
                t->normal = glm::normalize(glm::cross(t->vertex[1] - t->vertex[0], t->vertex[2] - t->vertex[0]));
                mesh->triangles.push_back(t);
                d_triangles.push_back(*t);
            }

            model->meshes.push_back(mesh);
        }
    }
    std::cout << "Loaded mesh with " << d_triangles.size() << " triangles from " << filename.c_str() << std::endl;
    return model;
}

static void addSceneGeometry(Geom type,
                             int mat_id,
                             glm::vec3 pos,
                             glm::vec3 rot,
                             glm::vec3 s,
                             std::string objfile) 
{
    // create a transform matrix from the pos, rot and s
    glm::mat4 translate = glm::translate(glm::mat4(), pos);
    glm::mat4 rotateX = glm::rotate(rot.x * (float)M_PI / 180.f, glm::vec3(1.0, 0.0, 0.0));
    glm::mat4 rotateY = glm::rotate(rot.y * (float)M_PI / 180.f, glm::vec3(0.0, 1.0, 0.0));
    glm::mat4 rotateZ = glm::rotate(rot.z * (float)M_PI / 180.f, glm::vec3(0.0, 0.0, 1.0));
    glm::mat4 scale = glm::scale(s);
    glm::mat4 transform = translate * rotateX * rotateY * rotateZ * scale;

    // determine what kind of geometry is added

    if (type == CUBE) {
        // A cube is made of 12 triangles -> 36 vertices
        // First create a unit cube, then transform the vertices. A unit cube has an edge length of 1.

        // Add the vertices
        d_vertices.push_back(toVertex(glm::vec3(-0.5f, 0.5f, -0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, 0.5f, -0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, 0.5f, 0.5f), transform));

        d_vertices.push_back(toVertex(glm::vec3(-0.5f, 0.5f, -0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(-0.5f, 0.5f, 0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, 0.5f, 0.5f), transform));

        d_vertices.push_back(toVertex(glm::vec3(-0.5f, 0.5f, 0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(-0.5f, -0.5f, 0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, -0.5f, 0.5f), transform));

        d_vertices.push_back(toVertex(glm::vec3(-0.5f, 0.5f, 0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, -0.5f, 0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, 0.5f, 0.5f), transform));

        d_vertices.push_back(toVertex(glm::vec3(0.5f, 0.5f, 0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, -0.5f, 0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, 0.5f, -0.5f), transform));

        d_vertices.push_back(toVertex(glm::vec3(0.5f, 0.5f, -0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, -0.5f, 0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, -0.5f, -0.5f), transform));

        d_vertices.push_back(toVertex(glm::vec3(-0.5f, 0.5f, -0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(-0.5f, -0.5f, -0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(-0.5f, 0.5f, 0.5f), transform));

        d_vertices.push_back(toVertex(glm::vec3(-0.5f, 0.5f, 0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(-0.5f, -0.5f, 0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(-0.5f, -0.5f, -0.5f), transform));

        d_vertices.push_back(toVertex(glm::vec3(-0.5f, -0.5f, 0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, -0.5f, 0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(-0.5f, -0.5f, -0.5f), transform));

        d_vertices.push_back(toVertex(glm::vec3(-0.5f, -0.5f, -0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, -0.5f, -0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, -0.5f, 0.5f), transform));

        d_vertices.push_back(toVertex(glm::vec3(-0.5f, 0.5f, -0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, 0.5f, -0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, -0.5f, -0.5f), transform));

        d_vertices.push_back(toVertex(glm::vec3(-0.5f, 0.5f, -0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(-0.5f, -0.5f, -0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, -0.5f, -0.5f), transform));

        TRIANGLE_COUNT += 12;

        // Add material id to mat indices
        for (int i = 0; i < 12; ++i)
            d_material_indices.push_back(mat_id);
    }
    else if (type == ICOSPHERE) {
        // a sphere can be created by subdividing an icosahedron
        // Source: http://blog.andreaskahler.com/2009/06/creating-icosphere-mesh-in-code.html

        // First, create the 12 vertices of an icosahedron -> an icosahedron has 20 faces
        float t = (1.f + sqrtf(5.f)) / 2.f;
        Vertex p0 = fixToUnitSphere({ -1.f, t, 0.f, 0.f });
        Vertex p1 = fixToUnitSphere({ 1.f, t, 0.f, 0.f });
        Vertex p2 = fixToUnitSphere({ -1.f, -t, 0.f, 0.f });
        Vertex p3 = fixToUnitSphere({ 1.f, -t, 0.f, 0.f });

        Vertex p4 = fixToUnitSphere({ 0.f, -1.f, t, 0.f });
        Vertex p5 = fixToUnitSphere({ 0.f, 1.f, t, 0.f });
        Vertex p6 = fixToUnitSphere({ 0.f, -1.f, -t, 0.f });
        Vertex p7 = fixToUnitSphere({ 0.f, 1.f, -t, 0.f });

        Vertex p8 = fixToUnitSphere({ t, 0.f, -1.f, 0.f });
        Vertex p9 = fixToUnitSphere({ t, 0.f, 1.f, 0.f });
        Vertex p10 = fixToUnitSphere({ -t, 0.f, -1.f, 0.f });
        Vertex p11 = fixToUnitSphere({ -t, 0.f, 1.f, 0.f });

        // create a temporary triangles vector and put all the 20 triangles into it
        // Each triangle is made of 3 consecutive vertices
        std::vector<Vertex> temp_triangles = { p0, p11, p5, p0, p5, p1, p0, p1, p7, p0, p7, p10, p0, p10, p11,
                                               p1, p5, p9, p5, p11, p4, p11, p10, p2, p10, p7, p6, p7, p1, p8,
                                               p3, p9, p4, p3, p4, p2, p3, p2, p6, p3, p6, p8, p3, p8, p9,
                                               p4, p9, p5, p2, p4, p11, p6, p2, p10, p8, p6, p7, p9, p8, p1 };
        
        // Each edge of the icosphere will be split in half -> this will create 4 subtriangles from 1 triangle
        int rec_level = 3; // default subdivision level is set to 3, we can change it later
        for (int i = 0; i < rec_level; ++i) {
            std::vector<Vertex> temp_triangles_2;
            for (int j = 0; j < temp_triangles.size(); j += 3) {
                
                // get the 3 vertices of the triangle
                Vertex v1 = temp_triangles[j];
                Vertex v2 = temp_triangles[j + 1];
                Vertex v3 = temp_triangles[j + 2];

                // replace current triangle with 4 triangles
                Vertex mid1 = fixToUnitSphere({ (v1.x + v2.x) / 2.f, (v1.y + v2.y) / 2.f, (v1.z + v2.z) / 2.f, 0.f });
                Vertex mid2 = fixToUnitSphere({ (v2.x + v3.x) / 2.f, (v2.y + v3.y) / 2.f, (v2.z + v3.z) / 2.f, 0.f });
                Vertex mid3 = fixToUnitSphere({ (v1.x + v3.x) / 2.f, (v1.y + v3.y) / 2.f, (v1.z + v3.z) / 2.f, 0.f });

                temp_triangles_2.push_back(v1);
                temp_triangles_2.push_back(mid1);
                temp_triangles_2.push_back(mid3);
                temp_triangles_2.push_back(v2);
                temp_triangles_2.push_back(mid2);
                temp_triangles_2.push_back(mid1);
                temp_triangles_2.push_back(v3);
                temp_triangles_2.push_back(mid3);
                temp_triangles_2.push_back(mid2);
                temp_triangles_2.push_back(mid1);
                temp_triangles_2.push_back(mid2);
                temp_triangles_2.push_back(mid3);
            }
            // ping-pong vectors
            temp_triangles = temp_triangles_2;
        }

        // Done with subdivision - now add the resulting vertices to the buffer as well the material indices per triangle
        int num_triangles = 0;
        for (int i = 0; i < temp_triangles.size(); ++i) {
            if (i % 3 == 0) {
                d_material_indices.push_back(mat_id);
                num_triangles++;
            }
            Vertex v = temp_triangles[i];
            d_vertices.push_back(toVertex(glm::vec3(v.x, v.y, v.z), transform));
        }
        TRIANGLE_COUNT += num_triangles;
    }
    else if (type == MESH) {
        if(objfile == "")
        {
            return;
        }
        Model* model = loadMesh(objfile);
        
        for (int i = 0; i < model->meshes.size(); ++i)
        {
            Mesh* mesh = model->meshes[i];
            int material_id = (model->material) ? addMaterial(DIFFUSE, make_float3(mesh->diffuse.x,mesh->diffuse.y,mesh->diffuse.z), make_float3(0.f), make_float3(0.f), 0.f, 0.f) : mat_id;
            for (int j = 0; j < mesh->triangles.size(); ++j)
            {
                d_material_indices.push_back(material_id);
                Triangle* t = mesh->triangles[j];
                d_vertices.push_back(toVertex(t->vertex[0], transform));
                d_vertices.push_back(toVertex(t->vertex[1], transform));
                d_vertices.push_back(toVertex(t->vertex[2], transform));
                TRIANGLE_COUNT += 1;
            }
        }
        d_triangles.clear();
        // arbitrary mesh load
        // TODO: Parse the obj file and add the vertices to d_vertices and for each triangle push the mat_id to d_material_indices
        // It seems like the current OptiX buffer structure doesn't use indexing so although it's inefficient, we push each vertex multiple times for closed geometry since vertices are shared across multiple faces
        // Don't forget to update TRIANGLE_COUNT
    }
    else if (type == AREA_LIGHT) {
        // We create area lights from 2-D planes
        // A plane is made of 2 triangles -> 6 vertices
        Vertex v1 = toVertex(glm::vec3(-0.5f, 0.f, -0.5f), transform);
        Vertex corner = toVertex(glm::vec3(0.5f, 0.f, -0.5f), transform);
        Vertex v2 = toVertex(glm::vec3(0.5f, 0.f, 0.5f), transform);
        d_vertices.push_back(v1); d_vertices.push_back(corner); d_vertices.push_back(v2);

        d_vertices.push_back(toVertex(glm::vec3(-0.5f, 0.f, -0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(-0.5f, 0.f, 0.5f), transform));
        d_vertices.push_back(toVertex(glm::vec3(0.5f, 0.f, 0.5f), transform));

        // Push the material id twice, one per triangle
        d_material_indices.push_back(mat_id);
        d_material_indices.push_back(mat_id);

        TRIANGLE_COUNT += 2;
        // Create a light if material is emissive
        if (d_mat_types[mat_id] == EMISSIVE) {
            float3 c = make_float3(corner.x, corner.y, corner.z); //corner
            float3 v1f = make_float3(v1.x - c.x, 0.f, 0.f); //v1
            float3 v2f = make_float3(0.f, 0.f, v2.z - c.z); //v2
            float3 n = normalize(-cross(v1f, v2f));
            d_lights.push_back({ AREA_LIGHT, c, v1f, v2f, n, d_emission_colors[mat_id], 0.f, 0.f });
        }
    }
    else if (type == POINT_LIGHT) {
        // We only allow point geometry for light sources
        if (d_mat_types[mat_id] != EMISSIVE) return;
        Vertex pos = toVertex(glm::vec3(0.f, 0.f, 0.f), transform);
        // We can't have the point light itself to be visible since points are not supported by our triangle GAS so we won't be adding it to d_vertices

        float3 pos_f = make_float3(pos.x, pos.y, pos.z);
        d_lights.push_back({ POINT_LIGHT, pos_f, pos_f, pos_f, make_float3(0.f), d_emission_colors[mat_id], 0.f, 0.f });
    }
    else if (type == SPOT_LIGHT) {
        // We only allow spot light geometry for light sources
        if (d_mat_types[mat_id] != EMISSIVE) return;
        // A spotlight is very similar to a point light in terms of being represented by a single point rather than triangle(s)
        // However, a spotlight needs additional light parameters to be set
        Vertex pos = toVertex(glm::vec3(0.f, 0.f, 0.f), transform);
        float3 pos_f = make_float3(pos.x, pos.y, pos.z);
        // The normal of point lights is simply the direction the spot light cone is facing
        glm::vec4 norm = rotateX * rotateY * rotateZ * glm::vec4(0.f, -1.f, 0.f, 1.f);
        float3 n = normalize(make_float3(norm.x, norm.y, norm.z));
        d_lights.push_back({ SPOT_LIGHT, pos_f, pos_f, pos_f, n, d_emission_colors[mat_id], glm::cos(25.f * (float)M_PI / 180.f), glm::cos(20.f * (float)M_PI / 180.f) });
    }
}

//------------------------------------------------------------------------------
//
// GLFW callbacks
//
//------------------------------------------------------------------------------

static void mouseButtonCallback( GLFWwindow* window, int button, int action, int mods )
{
    double xpos, ypos;
    glfwGetCursorPos( window, &xpos, &ypos );

    if( action == GLFW_PRESS )
    {
        mouse_button = button;
        trackball.startTracking( static_cast<int>( xpos ), static_cast<int>( ypos ) );
    }
    else
    {
        mouse_button = -1;
    }
}


static void cursorPosCallback( GLFWwindow* window, double xpos, double ypos )
{
    Params* params = static_cast<Params*>( glfwGetWindowUserPointer( window ) );

    if( mouse_button == GLFW_MOUSE_BUTTON_LEFT )
    {
        trackball.setViewMode( sutil::Trackball::LookAtFixed );
        trackball.updateTracking( static_cast<int>( xpos ), static_cast<int>( ypos ), params->width, params->height );
        camera_changed = true;
    }
    else if( mouse_button == GLFW_MOUSE_BUTTON_RIGHT )
    {
        trackball.setViewMode( sutil::Trackball::EyeFixed );
        trackball.updateTracking( static_cast<int>( xpos ), static_cast<int>( ypos ), params->width, params->height );
        camera_changed = true;
    }
}


static void windowSizeCallback( GLFWwindow* window, int32_t res_x, int32_t res_y )
{
    // Keep rendering at the current resolution when the window is minimized.
    if( minimized )
        return;

    // Output dimensions must be at least 1 in both x and y.
    sutil::ensureMinimumSize( res_x, res_y );

    Params* params = static_cast<Params*>( glfwGetWindowUserPointer( window ) );
    params->width  = res_x;
    params->height = res_y;
    camera_changed = true;
    resize_dirty   = true;
}


static void windowIconifyCallback( GLFWwindow* window, int32_t iconified )
{
    minimized = ( iconified > 0 );
}


static void keyCallback( GLFWwindow* window, int32_t key, int32_t /*scancode*/, int32_t action, int32_t /*mods*/ )
{
    if( action == GLFW_PRESS )
    {
        if( key == GLFW_KEY_Q || key == GLFW_KEY_ESCAPE )
        {
            glfwSetWindowShouldClose( window, true );
        }
    }
    else if( key == GLFW_KEY_S )
    {
        // Save the image
        saveRequested = true;
    }
}


static void scrollCallback( GLFWwindow* window, double xscroll, double yscroll )
{
    if( trackball.wheelEvent( (int)yscroll ) )
        camera_changed = true;
}


//------------------------------------------------------------------------------
//
// Helper functions
// TODO: some of these should move to sutil or optix util header
//
//------------------------------------------------------------------------------

void printUsageAndExit( const char* argv0 )
{
    std::cerr << "Usage  : " << argv0 << " [options]\n";
    std::cerr << "Options: --file | -f <filename>      File for image output\n";
    std::cerr << "         --launch-samples | -s       Number of samples per pixel per launch (default 16)\n";
    std::cerr << "         --no-gl-interop             Disable GL interop for display\n";
    std::cerr << "         --dim=<width>x<height>      Set image dimensions; defaults to 768x768\n";
    std::cerr << "         --help | -h                 Print this usage message\n";
    exit( 0 );
}


void initLaunchParams( PathTracerState& state )
{
    /* 
    * Copy light data to device
    */
    const size_t lights_size_in_bytes = d_lights.size() * sizeof(Light);
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&state.d_lights), lights_size_in_bytes));
    CUDA_CHECK(cudaMemcpy(
        reinterpret_cast<void*>(state.d_lights),
        d_lights.data(), lights_size_in_bytes,
        cudaMemcpyHostToDevice
    ));

    CUDA_CHECK( cudaMalloc(
                reinterpret_cast<void**>( &state.params.accum_buffer ),
                state.params.width * state.params.height * sizeof( float4 )
                ) );
    state.params.frame_buffer = nullptr;  // Will be set when output buffer is mapped

    state.params.samples_per_launch = samples_per_launch;
    state.params.depth = depth;
    state.params.subframe_index     = 0u;

    // Get light sources in the scene
    state.params.lights         = reinterpret_cast<Light*>(state.d_lights);
    state.params.num_lights     = d_lights.size();
    /*state.params.light.emission = make_float3( 10.0f, 10.0f, 10.0f );
    state.params.light.corner   = make_float3(-2.f, 9.95f, -2.f);
    state.params.light.v1       = make_float3( 0.0f, 0.0f, -2.0f );
    state.params.light.v2       = make_float3( 2.0f, 0.0f, 0.0f );
    state.params.light.normal   = normalize( cross( state.params.light.v1, state.params.light.v2 ) );*/
    state.params.handle         = state.gas_handle;

    CUDA_CHECK( cudaStreamCreate( &state.stream ) );
    CUDA_CHECK( cudaMalloc( reinterpret_cast<void**>( &state.d_params ), sizeof( Params ) ) );

}


void handleCameraUpdate( Params& params )
{
    if( !camera_changed )
        return;
    camera_changed = false;

    camera.setAspectRatio( static_cast<float>( params.width ) / static_cast<float>( params.height ) );
    params.eye = camera.eye();
    camera.UVWFrame( params.U, params.V, params.W );
}


void handleResize( sutil::CUDAOutputBuffer<uchar4>& output_buffer, Params& params )
{
    if( !resize_dirty )
        return;
    resize_dirty = false;

    output_buffer.resize( params.width, params.height );

    // Realloc accumulation buffer
    CUDA_CHECK( cudaFree( reinterpret_cast<void*>( params.accum_buffer ) ) );
    CUDA_CHECK( cudaMalloc(
                reinterpret_cast<void**>( &params.accum_buffer ),
                params.width * params.height * sizeof( float4 )
                ) );
}


void updateState( sutil::CUDAOutputBuffer<uchar4>& output_buffer, Params& params )
{
    // Update params on device
    if( camera_changed || resize_dirty )
        params.subframe_index = 0;

    handleCameraUpdate( params );
    handleResize( output_buffer, params );
}


void launchSubframe( sutil::CUDAOutputBuffer<uchar4>& output_buffer, PathTracerState& state )
{
    // Launch
    uchar4* result_buffer_data = output_buffer.map();
    state.params.frame_buffer  = result_buffer_data;
    CUDA_CHECK( cudaMemcpyAsync(
                reinterpret_cast<void*>( state.d_params ),
                &state.params, sizeof( Params ),
                cudaMemcpyHostToDevice, state.stream
                ) );

    OPTIX_CHECK( optixLaunch(
                state.pipeline,
                state.stream,
                reinterpret_cast<CUdeviceptr>( state.d_params ),
                sizeof( Params ),
                &state.sbt,
                state.params.width,   // launch width
                state.params.height,  // launch height
                1                     // launch depth
                ) );
    output_buffer.unmap();
    CUDA_SYNC_CHECK();
}


void displaySubframe( sutil::CUDAOutputBuffer<uchar4>& output_buffer, sutil::GLDisplay& gl_display, GLFWwindow* window )
{
    // Display
    int framebuf_res_x = 0;  // The display's resolution (could be HDPI res)
    int framebuf_res_y = 0;  //
    glfwGetFramebufferSize( window, &framebuf_res_x, &framebuf_res_y );
    gl_display.display(
            output_buffer.width(),
            output_buffer.height(),
            framebuf_res_x,
            framebuf_res_y,
            output_buffer.getPBO()
            );
}


static void context_log_cb( unsigned int level, const char* tag, const char* message, void* /*cbdata */ )
{
    std::cerr << "[" << std::setw( 2 ) << level << "][" << std::setw( 12 ) << tag << "]: " << message << "\n";
}


void initCameraState()
{
    camera.setEye( make_float3( 0.f, 5.f, 17.5f ) );
    camera.setLookat( make_float3( 0.f, 5.f, 0.f ) );
    camera.setUp( make_float3( 0.0f, 1.0f, 0.0f ) );
    camera.setFovY( 45.0f );
    camera_changed = true;

    trackball.setCamera( &camera );
    trackball.setMoveSpeed( 10.0f );
    trackball.setReferenceFrame(
            make_float3( 1.0f, 0.0f, 0.0f ),
            make_float3( 0.0f, 0.0f, 1.0f ),
            make_float3( 0.0f, 1.0f, 0.0f )
            );
    trackball.setGimbalLock( true );
}


void createContext( PathTracerState& state )
{
    // Initialize CUDA
    CUDA_CHECK( cudaFree( 0 ) );

    OptixDeviceContext context;
    CUcontext          cu_ctx = 0;  // zero means take the current context
    OPTIX_CHECK( optixInit() );
    OptixDeviceContextOptions options = {};
    options.logCallbackFunction       = &context_log_cb;
    options.logCallbackLevel          = 4;
    OPTIX_CHECK( optixDeviceContextCreate( cu_ctx, &options, &context ) );

    state.context = context;
}


void buildMeshAccel( PathTracerState& state )
{
    //
    // copy mesh data to device
    //
    const size_t vertices_size_in_bytes = d_vertices.size() * sizeof( Vertex );
    CUDA_CHECK( cudaMalloc( reinterpret_cast<void**>( &state.d_vertices ), vertices_size_in_bytes ) );
    CUDA_CHECK( cudaMemcpy(
                reinterpret_cast<void*>( state.d_vertices ),
                d_vertices.data(), vertices_size_in_bytes,
                cudaMemcpyHostToDevice
                ) );

    CUdeviceptr  d_mat_indices             = 0;
    const size_t mat_indices_size_in_bytes = d_material_indices.size() * sizeof( uint32_t );
    CUDA_CHECK( cudaMalloc( reinterpret_cast<void**>( &d_mat_indices ), mat_indices_size_in_bytes ) );
    CUDA_CHECK( cudaMemcpy(
                reinterpret_cast<void*>( d_mat_indices ),
                d_material_indices.data(),
                mat_indices_size_in_bytes,
                cudaMemcpyHostToDevice
                ) );

    //
    // Build triangle GAS
    //

    std::vector<uint32_t> triangle_input_flags; // One per SBT record for this build input
    for (int i = 0; i < MAT_COUNT; ++i) {
        triangle_input_flags.push_back(OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT);
    }

    OptixBuildInput triangle_input                           = {};
    triangle_input.type                                      = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    triangle_input.triangleArray.vertexFormat                = OPTIX_VERTEX_FORMAT_FLOAT3;
    triangle_input.triangleArray.vertexStrideInBytes         = sizeof( Vertex );
    triangle_input.triangleArray.numVertices                 = static_cast<uint32_t>( d_vertices.size() );
    triangle_input.triangleArray.vertexBuffers               = &state.d_vertices;
    triangle_input.triangleArray.flags                       = triangle_input_flags.data();
    triangle_input.triangleArray.numSbtRecords               = MAT_COUNT;
    triangle_input.triangleArray.sbtIndexOffsetBuffer        = d_mat_indices;
    triangle_input.triangleArray.sbtIndexOffsetSizeInBytes   = sizeof( uint32_t );
    triangle_input.triangleArray.sbtIndexOffsetStrideInBytes = sizeof( uint32_t );

    OptixAccelBuildOptions accel_options = {};
    accel_options.buildFlags             = OPTIX_BUILD_FLAG_ALLOW_COMPACTION;
    accel_options.operation              = OPTIX_BUILD_OPERATION_BUILD;

    OptixAccelBufferSizes gas_buffer_sizes;
    OPTIX_CHECK( optixAccelComputeMemoryUsage(
                state.context,
                &accel_options,
                &triangle_input,
                1,  // num_build_inputs
                &gas_buffer_sizes
                ) );

    CUdeviceptr d_temp_buffer;
    CUDA_CHECK( cudaMalloc( reinterpret_cast<void**>( &d_temp_buffer ), gas_buffer_sizes.tempSizeInBytes ) );

    // non-compacted output
    CUdeviceptr d_buffer_temp_output_gas_and_compacted_size;
    size_t      compactedSizeOffset = roundUp<size_t>( gas_buffer_sizes.outputSizeInBytes, 8ull );
    CUDA_CHECK( cudaMalloc(
                reinterpret_cast<void**>( &d_buffer_temp_output_gas_and_compacted_size ),
                compactedSizeOffset + 8
                ) );

    OptixAccelEmitDesc emitProperty = {};
    emitProperty.type               = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
    emitProperty.result             = ( CUdeviceptr )( (char*)d_buffer_temp_output_gas_and_compacted_size + compactedSizeOffset );

    OPTIX_CHECK( optixAccelBuild(
                state.context,
                0,                                  // CUDA stream
                &accel_options,
                &triangle_input,
                1,                                  // num build inputs
                d_temp_buffer,
                gas_buffer_sizes.tempSizeInBytes,
                d_buffer_temp_output_gas_and_compacted_size,
                gas_buffer_sizes.outputSizeInBytes,
                &state.gas_handle,
                &emitProperty,                      // emitted property list
                1                                   // num emitted properties
                ) );

    CUDA_CHECK( cudaFree( reinterpret_cast<void*>( d_temp_buffer ) ) );
    CUDA_CHECK( cudaFree( reinterpret_cast<void*>( d_mat_indices ) ) );

    size_t compacted_gas_size;
    CUDA_CHECK( cudaMemcpy( &compacted_gas_size, (void*)emitProperty.result, sizeof(size_t), cudaMemcpyDeviceToHost ) );

    if( compacted_gas_size < gas_buffer_sizes.outputSizeInBytes )
    {
        CUDA_CHECK( cudaMalloc( reinterpret_cast<void**>( &state.d_gas_output_buffer ), compacted_gas_size ) );

        // use handle as input and output
        OPTIX_CHECK( optixAccelCompact( state.context, 0, state.gas_handle, state.d_gas_output_buffer, compacted_gas_size, &state.gas_handle ) );

        CUDA_CHECK( cudaFree( (void*)d_buffer_temp_output_gas_and_compacted_size ) );
    }
    else
    {
        state.d_gas_output_buffer = d_buffer_temp_output_gas_and_compacted_size;
    }
}


void createModule( PathTracerState& state )
{
    OptixModuleCompileOptions module_compile_options = {};
    module_compile_options.maxRegisterCount  = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
    module_compile_options.optLevel          = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    module_compile_options.debugLevel        = OPTIX_COMPILE_DEBUG_LEVEL_LINEINFO;

    state.pipeline_compile_options.usesMotionBlur        = false;
    state.pipeline_compile_options.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_GAS;
    state.pipeline_compile_options.numPayloadValues      = 2;
    state.pipeline_compile_options.numAttributeValues    = 2;
#ifdef DEBUG // Enables debug exceptions during optix launches. This may incur significant performance cost and should only be done during development.
    state.pipeline_compile_options.exceptionFlags = OPTIX_EXCEPTION_FLAG_DEBUG | OPTIX_EXCEPTION_FLAG_TRACE_DEPTH | OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW;
#else
    state.pipeline_compile_options.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
#endif
    state.pipeline_compile_options.pipelineLaunchParamsVariableName = "params";

    const std::string ptx = sutil::getPtxString( OPTIX_SAMPLE_NAME, OPTIX_SAMPLE_DIR, "optixPathTracer.cu" );

    char   log[2048];
    size_t sizeof_log = sizeof( log );
    OPTIX_CHECK_LOG( optixModuleCreateFromPTX(
                state.context,
                &module_compile_options,
                &state.pipeline_compile_options,
                ptx.c_str(),
                ptx.size(),
                log,
                &sizeof_log,
                &state.ptx_module
                ) );
}


void createProgramGroups( PathTracerState& state )
{
    OptixProgramGroupOptions  program_group_options = {};

    char   log[2048];
    size_t sizeof_log = sizeof( log );

    {
        OptixProgramGroupDesc raygen_prog_group_desc    = {};
        raygen_prog_group_desc.kind                     = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        raygen_prog_group_desc.raygen.module            = state.ptx_module;
        raygen_prog_group_desc.raygen.entryFunctionName = "__raygen__rg";

        OPTIX_CHECK_LOG( optixProgramGroupCreate(
                    state.context, &raygen_prog_group_desc,
                    1,  // num program groups
                    &program_group_options,
                    log,
                    &sizeof_log,
                    &state.raygen_prog_group
                    ) );
    }

    {
        OptixProgramGroupDesc miss_prog_group_desc  = {};
        miss_prog_group_desc.kind                   = OPTIX_PROGRAM_GROUP_KIND_MISS;
        miss_prog_group_desc.miss.module            = state.ptx_module;
        miss_prog_group_desc.miss.entryFunctionName = "__miss__radiance";
        sizeof_log                                  = sizeof( log );
        OPTIX_CHECK_LOG( optixProgramGroupCreate(
                    state.context, &miss_prog_group_desc,
                    1,  // num program groups
                    &program_group_options,
                    log, &sizeof_log,
                    &state.radiance_miss_group
                    ) );

        memset( &miss_prog_group_desc, 0, sizeof( OptixProgramGroupDesc ) );
        miss_prog_group_desc.kind                   = OPTIX_PROGRAM_GROUP_KIND_MISS;
        miss_prog_group_desc.miss.module            = nullptr;  // NULL miss program for occlusion rays
        miss_prog_group_desc.miss.entryFunctionName = nullptr;
        sizeof_log                                  = sizeof( log );
        OPTIX_CHECK_LOG( optixProgramGroupCreate(
                    state.context, &miss_prog_group_desc,
                    1,  // num program groups
                    &program_group_options,
                    log,
                    &sizeof_log,
                    &state.occlusion_miss_group
                    ) );
    }

    {
        OptixProgramGroupDesc hit_prog_group_desc        = {};
        hit_prog_group_desc.kind                         = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
        hit_prog_group_desc.hitgroup.moduleCH            = state.ptx_module;
        hit_prog_group_desc.hitgroup.entryFunctionNameCH = "__closesthit__radiance";
        sizeof_log                                       = sizeof( log );
        OPTIX_CHECK_LOG( optixProgramGroupCreate(
                    state.context,
                    &hit_prog_group_desc,
                    1,  // num program groups
                    &program_group_options,
                    log,
                    &sizeof_log,
                    &state.radiance_hit_group
                    ) );

        memset( &hit_prog_group_desc, 0, sizeof( OptixProgramGroupDesc ) );
        hit_prog_group_desc.kind                         = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
        hit_prog_group_desc.hitgroup.moduleCH            = state.ptx_module;
        hit_prog_group_desc.hitgroup.entryFunctionNameCH = "__closesthit__occlusion";
        sizeof_log                                       = sizeof( log );
        OPTIX_CHECK( optixProgramGroupCreate(
                    state.context,
                    &hit_prog_group_desc,
                    1,  // num program groups
                    &program_group_options,
                    log,
                    &sizeof_log,
                    &state.occlusion_hit_group
                    ) );
    }
}


void createPipeline( PathTracerState& state )
{
    OptixProgramGroup program_groups[] =
    {
        state.raygen_prog_group,
        state.radiance_miss_group,
        state.occlusion_miss_group,
        state.radiance_hit_group,
        state.occlusion_hit_group
    };

    OptixPipelineLinkOptions pipeline_link_options = {};
    pipeline_link_options.maxTraceDepth            = 2;
    pipeline_link_options.debugLevel               = OPTIX_COMPILE_DEBUG_LEVEL_FULL;

    char   log[2048];
    size_t sizeof_log = sizeof( log );
    OPTIX_CHECK_LOG( optixPipelineCreate(
                state.context,
                &state.pipeline_compile_options,
                &pipeline_link_options,
                program_groups,
                sizeof( program_groups ) / sizeof( program_groups[0] ),
                log,
                &sizeof_log,
                &state.pipeline
                ) );

    // We need to specify the max traversal depth.  Calculate the stack sizes, so we can specify all
    // parameters to optixPipelineSetStackSize.
    OptixStackSizes stack_sizes = {};
    OPTIX_CHECK( optixUtilAccumulateStackSizes( state.raygen_prog_group,    &stack_sizes ) );
    OPTIX_CHECK( optixUtilAccumulateStackSizes( state.radiance_miss_group,  &stack_sizes ) );
    OPTIX_CHECK( optixUtilAccumulateStackSizes( state.occlusion_miss_group, &stack_sizes ) );
    OPTIX_CHECK( optixUtilAccumulateStackSizes( state.radiance_hit_group,   &stack_sizes ) );
    OPTIX_CHECK( optixUtilAccumulateStackSizes( state.occlusion_hit_group,  &stack_sizes ) );

    uint32_t max_trace_depth = 2;
    uint32_t max_cc_depth = 0;
    uint32_t max_dc_depth = 0;
    uint32_t direct_callable_stack_size_from_traversal;
    uint32_t direct_callable_stack_size_from_state;
    uint32_t continuation_stack_size;
    OPTIX_CHECK( optixUtilComputeStackSizes(
                &stack_sizes,
                max_trace_depth,
                max_cc_depth,
                max_dc_depth,
                &direct_callable_stack_size_from_traversal,
                &direct_callable_stack_size_from_state,
                &continuation_stack_size
                ) );

    const uint32_t max_traversal_depth = 1;
    OPTIX_CHECK( optixPipelineSetStackSize(
                state.pipeline,
                direct_callable_stack_size_from_traversal,
                direct_callable_stack_size_from_state,
                continuation_stack_size,
                max_traversal_depth
                ) );
}


void createSBT( PathTracerState& state )
{
    CUdeviceptr  d_raygen_record;
    const size_t raygen_record_size = sizeof( RayGenRecord );
    CUDA_CHECK( cudaMalloc( reinterpret_cast<void**>( &d_raygen_record ), raygen_record_size ) );

    RayGenRecord rg_sbt = {};
    OPTIX_CHECK( optixSbtRecordPackHeader( state.raygen_prog_group, &rg_sbt ) );

    CUDA_CHECK( cudaMemcpy(
                reinterpret_cast<void*>( d_raygen_record ),
                &rg_sbt,
                raygen_record_size,
                cudaMemcpyHostToDevice
                ) );


    CUdeviceptr  d_miss_records;
    const size_t miss_record_size = sizeof( MissRecord );
    CUDA_CHECK( cudaMalloc( reinterpret_cast<void**>( &d_miss_records ), miss_record_size * RAY_TYPE_COUNT ) );

    MissRecord ms_sbt[2];
    OPTIX_CHECK( optixSbtRecordPackHeader( state.radiance_miss_group,  &ms_sbt[0] ) );
    ms_sbt[0].data.bg_color = make_float4( 0.0f );
    OPTIX_CHECK( optixSbtRecordPackHeader( state.occlusion_miss_group, &ms_sbt[1] ) );
    ms_sbt[1].data.bg_color = make_float4( 0.0f );

    CUDA_CHECK( cudaMemcpy(
                reinterpret_cast<void*>( d_miss_records ),
                ms_sbt,
                miss_record_size*RAY_TYPE_COUNT,
                cudaMemcpyHostToDevice
                ) );

    CUdeviceptr  d_hitgroup_records;
    const size_t hitgroup_record_size = sizeof( HitGroupRecord );
    CUDA_CHECK( cudaMalloc(
                reinterpret_cast<void**>( &d_hitgroup_records ),
                hitgroup_record_size * RAY_TYPE_COUNT * MAT_COUNT
                ) );

    std::vector<HitGroupRecord> hitgroup_records;
    for (int i = 0; i < RAY_TYPE_COUNT * MAT_COUNT; ++i) {
        hitgroup_records.push_back(HitGroupRecord());
    }
    for( int i = 0; i < MAT_COUNT; ++i )
    {
        {
            const int sbt_idx = i * RAY_TYPE_COUNT + 0;  // SBT for radiance ray-type for ith material

            OPTIX_CHECK( optixSbtRecordPackHeader( state.radiance_hit_group, &hitgroup_records[sbt_idx] ) );
            hitgroup_records[sbt_idx].data.emission_color = d_emission_colors[i];
            hitgroup_records[sbt_idx].data.diffuse_color  = d_diffuse_colors[i];
            hitgroup_records[sbt_idx].data.specular_color = d_spec_colors[i];
            hitgroup_records[sbt_idx].data.spec_exp       = d_spec_exp[i];
            hitgroup_records[sbt_idx].data.ior            = d_ior[i];
            hitgroup_records[sbt_idx].data.vertices       = reinterpret_cast<float4*>( state.d_vertices );
            hitgroup_records[sbt_idx].data.mat            = d_mat_types[i];
        }

        {
            const int sbt_idx = i * RAY_TYPE_COUNT + 1;  // SBT for occlusion ray-type for ith material
            memset( &hitgroup_records[sbt_idx], 0, hitgroup_record_size );

            OPTIX_CHECK( optixSbtRecordPackHeader( state.occlusion_hit_group, &hitgroup_records[sbt_idx] ) );
        }
    }

    CUDA_CHECK( cudaMemcpy(
                reinterpret_cast<void*>( d_hitgroup_records ),
                hitgroup_records.data(),
                hitgroup_record_size*RAY_TYPE_COUNT*MAT_COUNT,
                cudaMemcpyHostToDevice
                ) );

    state.sbt.raygenRecord                = d_raygen_record;
    state.sbt.missRecordBase              = d_miss_records;
    state.sbt.missRecordStrideInBytes     = static_cast<uint32_t>( miss_record_size );
    state.sbt.missRecordCount             = RAY_TYPE_COUNT;
    state.sbt.hitgroupRecordBase          = d_hitgroup_records;
    state.sbt.hitgroupRecordStrideInBytes = static_cast<uint32_t>( hitgroup_record_size );
    state.sbt.hitgroupRecordCount         = RAY_TYPE_COUNT * MAT_COUNT;
}


void cleanupState( PathTracerState& state )
{
    OPTIX_CHECK( optixPipelineDestroy( state.pipeline ) );
    OPTIX_CHECK( optixProgramGroupDestroy( state.raygen_prog_group ) );
    OPTIX_CHECK( optixProgramGroupDestroy( state.radiance_miss_group ) );
    OPTIX_CHECK( optixProgramGroupDestroy( state.radiance_hit_group ) );
    OPTIX_CHECK( optixProgramGroupDestroy( state.occlusion_hit_group ) );
    OPTIX_CHECK( optixProgramGroupDestroy( state.occlusion_miss_group ) );
    OPTIX_CHECK( optixModuleDestroy( state.ptx_module ) );
    OPTIX_CHECK( optixDeviceContextDestroy( state.context ) );


    CUDA_CHECK( cudaFree( reinterpret_cast<void*>( state.sbt.raygenRecord ) ) );
    CUDA_CHECK( cudaFree( reinterpret_cast<void*>( state.sbt.missRecordBase ) ) );
    CUDA_CHECK( cudaFree( reinterpret_cast<void*>( state.sbt.hitgroupRecordBase ) ) );
    CUDA_CHECK( cudaFree( reinterpret_cast<void*>( state.d_vertices ) ) );
    CUDA_CHECK( cudaFree( reinterpret_cast<void*>( state.d_lights ) ) );
    CUDA_CHECK( cudaFree( reinterpret_cast<void*>( state.d_gas_output_buffer ) ) );
    CUDA_CHECK( cudaFree( reinterpret_cast<void*>( state.params.accum_buffer ) ) );
    CUDA_CHECK( cudaFree( reinterpret_cast<void*>( state.d_params ) ) );
}


//------------------------------------------------------------------------------
//
// Main
//
//------------------------------------------------------------------------------

int main( int argc, char* argv[] )
{
    PathTracerState state;
    state.params.width                             = 768;
    state.params.height                            = 768;
    sutil::CUDAOutputBufferType output_buffer_type = sutil::CUDAOutputBufferType::GL_INTEROP;

    //
    // Parse command line options
    //
    std::string outfile;

    for( int i = 1; i < argc; ++i )
    {
        const std::string arg = argv[i];
        if( arg == "--help" || arg == "-h" )
        {
            printUsageAndExit( argv[0] );
        }
        else if( arg == "--no-gl-interop" )
        {
            output_buffer_type = sutil::CUDAOutputBufferType::CUDA_DEVICE;
        }
        else if( arg == "--file" || arg == "-f" )
        {
            if( i >= argc - 1 )
                printUsageAndExit( argv[0] );
            outfile = argv[++i];
        }
        else if( arg.substr( 0, 6 ) == "--dim=" )
        {
            const std::string dims_arg = arg.substr( 6 );
            int w, h;
            sutil::parseDimensions( dims_arg.c_str(), w, h );
            state.params.width  = w;
            state.params.height = h;
        }
        else if( arg == "--launch-samples" || arg == "-s" )
        {
            if( i >= argc - 1 )
                printUsageAndExit( argv[0] );
            samples_per_launch = atoi( argv[++i] );
        }
        else
        {
            std::cerr << "Unknown option '" << argv[i] << "'\n";
            printUsageAndExit( argv[0] );
        }
    }

    try
    {
        initCameraState();

        // Set up the scene
        // First add materials
        addMaterial(EMISSIVE, make_float3(1.f, 1.f, 1.f), make_float3(0.f), make_float3(5.f, 1.f, 1.f), 0.f, 0.f); // light material
        addMaterial(EMISSIVE, make_float3(1.f, 1.f, 1.f), make_float3(0.f), make_float3(20.f, 20.f, 20.f), 0.f, 0.f); // light material
        addMaterial(EMISSIVE, make_float3(1.f, 1.f, 1.f), make_float3(0.f), make_float3(5.f, 5.f, 5.f), 0.f, 0.f); // light material
        addMaterial(DIFFUSE, make_float3(1.f, 1.f, 1.f), make_float3(0.f), make_float3(0.f), 0.f, 0.f); // diffuse white
        addMaterial(DIFFUSE, make_float3(0.05f, 0.80f, 0.80f), make_float3(0.f), make_float3(0.f), 25.5f, 0.f); // diffuse cyan
        addMaterial(DIFFUSE, make_float3(0.80f, 0.05f, 0.80f), make_float3(0.f), make_float3(0.f), 0.f, 0.f); // diffuse magenta
        addMaterial(FRESNEL, make_float3(1.f, 0.60f, 0.80f), make_float3(1.f, 0.60f, 0.80f), make_float3(0.f), 0.f, 5.4f); // pink fresnel
        addMaterial(FRESNEL, make_float3(1.f, 1.f, 1.f), make_float3(1.f, 1.f, 1.f), make_float3(0.f), 0.f, 5.4f); // white fresnel
        addMaterial(GLOSSY, make_float3(0.80f, 0.80f, 0.80f), make_float3(0.80f, 0.80f, 0.80f), make_float3(0.f), 10.f, 0.f); // glossy white 1
        addMaterial(GLOSSY, make_float3(0.80f, 0.80f, 0.80f), make_float3(0.80f, 0.80f, 0.80f), make_float3(0.f), 40.f, 0.f); // glossy white 2
        addMaterial(MIRROR, make_float3(1.f, 1.f, 1.f), make_float3(1.f, 1.f, 1.f), make_float3(0.f), 0.f, 0.f); // perfect specular mirror

        // Then add geometry
        addSceneGeometry(POINT_LIGHT, 1, glm::vec3(0, 10, 0), glm::vec3(0, 0, 0), glm::vec3(3, .3, 3), ""); // ceiling light
        addSceneGeometry(MESH, 3, glm::vec3(-10, 0, 0), glm::vec3(0, 0, 0), glm::vec3(0.03, 0.03, 0.03), "../../data/Sponza/sponza.obj");

        //
        // Set up OptiX state
        //
        createContext( state );
        buildMeshAccel( state );
        createModule( state );
        createProgramGroups( state );
        createPipeline( state );
        createSBT( state );
        initLaunchParams( state );


        if( outfile.empty() )
        {
            GLFWwindow* window = sutil::initUI( "optixPathTracer", state.params.width, state.params.height );
            glfwSetMouseButtonCallback( window, mouseButtonCallback );
            glfwSetCursorPosCallback( window, cursorPosCallback );
            glfwSetWindowSizeCallback( window, windowSizeCallback );
            glfwSetWindowIconifyCallback( window, windowIconifyCallback );
            glfwSetKeyCallback( window, keyCallback );
            glfwSetScrollCallback( window, scrollCallback );
            glfwSetWindowUserPointer( window, &state.params );

            outfile = "output.png";

            //
            // Render loop
            //
            {
                sutil::CUDAOutputBuffer<uchar4> output_buffer(
                        output_buffer_type,
                        state.params.width,
                        state.params.height
                        );

                output_buffer.setStream( state.stream );
                sutil::GLDisplay gl_display;

                std::chrono::duration<double> state_update_time( 0.0 );
                std::chrono::duration<double> render_time( 0.0 );
                std::chrono::duration<double> display_time( 0.0 );

                do
                {
                    if (saveRequested) {
                        sutil::ImageBuffer buffer;
                        buffer.data = output_buffer.getHostPointer();
                        buffer.width = output_buffer.width();
                        buffer.height = output_buffer.height();
                        buffer.pixel_format = sutil::BufferImageFormat::UNSIGNED_BYTE4;
                        sutil::saveImage(outfile.c_str(), buffer, false);
                        saveRequested = false;
                    }
                    auto t0 = std::chrono::steady_clock::now();
                    glfwPollEvents();

                    updateState( output_buffer, state.params );
                    auto t1 = std::chrono::steady_clock::now();
                    state_update_time += t1 - t0;
                    t0 = t1;
                    
                    launchSubframe(output_buffer, state);
                    t1 = std::chrono::steady_clock::now();
                    render_time += t1 - t0;
                    t0 = t1;

                    displaySubframe(output_buffer, gl_display, window);
                    t1 = std::chrono::steady_clock::now();
                    display_time += t1 - t0;

                    sutil::displayStats( state_update_time, render_time, display_time );

                    glfwSwapBuffers( window );

                    ++state.params.subframe_index;
                } while( !glfwWindowShouldClose( window ));
                CUDA_SYNC_CHECK();
            }

            sutil::cleanupUI( window );
        }
        else
        {
            if( output_buffer_type == sutil::CUDAOutputBufferType::GL_INTEROP )
            {
                sutil::initGLFW();  // For GL context
                sutil::initGL();
            }

            sutil::CUDAOutputBuffer<uchar4> output_buffer(
                    output_buffer_type,
                    state.params.width,
                    state.params.height
                    );

            handleCameraUpdate( state.params );
            handleResize( output_buffer, state.params );
            launchSubframe( output_buffer, state );

            sutil::ImageBuffer buffer;
            buffer.data         = output_buffer.getHostPointer();
            buffer.width        = output_buffer.width();
            buffer.height       = output_buffer.height();
            buffer.pixel_format = sutil::BufferImageFormat::UNSIGNED_BYTE4;

            sutil::saveImage( outfile.c_str(), buffer, false );

            if( output_buffer_type == sutil::CUDAOutputBufferType::GL_INTEROP )
            {
                glfwTerminate();
            }
        }

        cleanupState( state );
    }
    catch( std::exception& e )
    {
        std::cerr << "Caught exception: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
