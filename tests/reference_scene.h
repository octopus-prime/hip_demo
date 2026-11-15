#pragma once

#include "scene/defaults.h"
#include "components/object/intersection.h"

namespace hip_demo {

struct SceneObject {
    float3 center;
    bool sphere;
    float tilt_sine;
    float tilt_cosine;
};

__host__ __device__ inline SceneObject scene_object(unsigned int index, bool detail = false) {
    if (detail) {
        if (index == 0) {
            return {{-1.68f, detail_sphere_radius, -0.70f}, true, 0.0f, 1.0f};
        }
        if (index == 1) {
            return {{1.55f, detail_sphere_radius, 0.9f}, true, 0.0f, 1.0f};
        }
        return {detail_torus_center, false, 0.0f, 1.0f};
    }
    const bool sphere = index % 2 == 0;
    const unsigned int tilt = index / 2;
    constexpr float sin60 = 0.86602540378f;
    const float sine = sphere ? 0.0f : (tilt == 0 ? -sin60 : tilt == 1 ? -0.5f : 0.5f);
    const float cosine = sphere ? 1.0f : (tilt == 0 ? 0.5f : sin60);
    const float absolute_sine = sine < 0.0f ? -sine : sine;
    float x = 0.0f;
    float z = -0.9f;
    switch (index) {
    case 0: x = -1.65f; break;
    case 1: x = -0.50f; break;
    case 2: x = 0.75f; break;
    case 3: x = 1.90f; break;
    case 4: x = -1.05f; z = 0.85f; break;
    case 5: x = 0.75f; z = 0.85f; break;
    case 6: x = -2.05f; z = 0.85f; break;
    }
    return {{x, sphere ? sphere_radius : torus_minor_radius + torus_major_radius * absolute_sine, z},
            sphere, sine, cosine};
}

__device__ inline void set_object_material(Hit& hit, unsigned int object_id, bool detail = false) {
    hit.roughness = 0.0f;
    if (detail) {
        if (object_id == 0) {
            hit.albedo = {0.96f, 0.96f, 0.96f};
            hit.material = ShaderKind::metal;
        } else if (object_id == 1) {
            hit.albedo = {0.96f, 0.98f, 1.0f};
            hit.material = ShaderKind::glass;
        } else {
            hit.albedo = {1.0f, 1.0f, 1.0f};
            hit.material = ShaderKind::emissive;
        }
        return;
    }
    switch (object_id) {
    case 0:
        hit.albedo = {0.92f, 0.58f, 0.22f};
        hit.material = ShaderKind::metal;
        hit.roughness = 0.08f;
        break;
    case 1:
    case 6:
        hit.albedo = {0.96f, 0.98f, 1.0f};
        hit.material = ShaderKind::glass;
        break;
    case 2:
        hit.albedo = {0.12f, 0.28f, 0.82f};
        hit.material = ShaderKind::phong;
        break;
    case 3:
        hit.albedo = {0.85f, 0.48f, 0.08f};
        hit.material = ShaderKind::phong;
        break;
    case 4:
        hit.albedo = {0.96f, 0.96f, 0.96f};
        hit.material = ShaderKind::metal;
        break;
    default:
        hit.albedo = {0.55f, 0.15f, 0.55f};
        hit.material = ShaderKind::diffuse;
        break;
    }
}

__device__ inline bool hit_primitive(const Ray& ray, unsigned int object_id, float maximum_distance, Hit& hit,
                              bool detail = false) {
    const SceneObject object = scene_object(object_id, detail);
    bool found;
    if (object.sphere) {
        found = Sphere{{object.center.x, object.center.y, object.center.z},
                       detail ? detail_sphere_radius : sphere_radius}.intersect(ray, 0.001f, maximum_distance, hit);
    } else {
        const Ray local_ray{
            rotate_x(ray.origin - object.center, -object.tilt_sine, object.tilt_cosine),
            rotate_x(ray.direction, -object.tilt_sine, object.tilt_cosine),
        };
        found = Torus{{}, detail ? detail_torus_major_radius : torus_major_radius,
                      detail ? detail_torus_minor_radius : torus_minor_radius}
            .intersect(local_ray, 0.001f, maximum_distance, hit);
        if (found) {
            hit.point = ray.origin + hit.distance * ray.direction;
            hit.normal = rotate_x(hit.normal, object.tilt_sine, object.tilt_cosine);
            hit.geometric_normal = hit.normal;
        }
    }
    if (found) {
        hit.object_id = object_id;
        set_object_material(hit, object_id, detail);
    }
    return found;
}

__device__ inline float3 light_radiance(bool detail = false) {
    return detail ? float3{24.0f, 23.0f, 21.0f} : float3{12.0f, 11.5f, 10.5f};
}

}
