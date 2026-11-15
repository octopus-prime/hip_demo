#pragma once

#include "math/vector.h"
#include "gpu/types.h"
#include <cstdint>

namespace hip_demo {

struct Ray {
    float3 origin;
    float3 direction;
};

struct Hit {
    float distance;
    float3 point;
    float3 normal;
    float3 geometric_normal;
    float3 albedo;
    ShaderKind material;
    float roughness;
    TextureKind texture;
    bool front_face;
    bool has_normal_map;
    unsigned int object_id;
};

__device__ inline void set_material(Hit& hit, const GpuMaterial& material) {
    hit.albedo = vector3(material.albedo);
    hit.material = material.kind;
    hit.roughness = material.roughness;
    hit.texture = material.texture;
}

__device__ inline void set_face_normal(Hit& hit, const Ray& ray, float3 outward_normal) {
    hit.front_face = dot(ray.direction, outward_normal) < 0.0f;
    hit.normal = hit.front_face ? outward_normal : -outward_normal;
    hit.geometric_normal = hit.normal;
    hit.has_normal_map = false;
}

}
