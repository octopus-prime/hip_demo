#pragma once

#include "math/vector.h"
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
    float3 albedo;
    Material material;
    float roughness;
    bool front_face;
    bool tiled_floor;
    unsigned int object_id;
};

__device__ inline void set_material(Hit& hit, const SceneMaterial& material) {
    hit.albedo = vector3(material.albedo);
    hit.material = material.kind;
    hit.roughness = material.roughness;
}

__device__ inline void set_face_normal(Hit& hit, const Ray& ray, float3 outward_normal) {
    hit.front_face = dot(ray.direction, outward_normal) < 0.0f;
    hit.normal = hit.front_face ? outward_normal : -outward_normal;
    hit.tiled_floor = false;
}

}
