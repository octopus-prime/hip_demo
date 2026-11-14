#pragma once

#include "gpu/scene_view.h"
#include "surface/room.h"
#include "shader/room.h"

namespace hip_demo {

__device__ inline bool hit_room(const Ray& ray, float maximum_distance, Hit& hit, bool detail = false,
                                const SceneSettings* settings = nullptr) {
    int wall = 0;
    if (!intersect_room(ray, maximum_distance, hit, wall, settings)) {
        return false;
    }
    shade_room(hit, ray, wall, detail, settings);
    return true;
}

__device__ inline bool hit_custom_objects(const Ray& ray, Hit& closest_hit, TraceScene scene) {
    if (!scene.geometry) return false;
    bool found = false;
    float closest_distance = 1.0e30f;
    Hit candidate{};

    hiprtRay traversal_ray{};
    traversal_ray.origin = ray.origin;
    traversal_ray.direction = ray.direction;
    traversal_ray.minT = 0.001f;
    traversal_ray.maxT = closest_distance;
    hiprtGeomCustomTraversalClosest traversal(
        scene.geometry, traversal_ray, hiprtTraversalHintDefault, nullptr, scene.functions);
    const hiprtHit result = traversal.getNextHit();
    if (result.hasHit()) {
        candidate.distance = result.t;
        candidate.point = ray.origin + result.t * ray.direction;
        set_face_normal(candidate, ray, result.normal);
        if (result.primID < scene.primitive_count) {
            candidate.object_id = result.primID;
            set_material(candidate, scene.primitives[result.primID].material);
        } else if (result.primID - scene.primitive_count < scene.csg_count) {
            candidate.object_id = result.primID;
            set_material(candidate, scene.csgs[result.primID - scene.primitive_count].material);
        } else {
            const auto& glyph = scene.glyphs[result.primID - scene.primitive_count - scene.csg_count];
            candidate.object_id = glyph.object_id;
            set_material(candidate, glyph.material);
        }
        closest_hit = candidate;
        found = true;
    }
    return found;
}

__device__ inline bool hit_objects(const Ray& ray, Hit& closest_hit, TraceScene scene) {
    bool found = hit_custom_objects(ray, closest_hit, scene);
    if (!scene.mesh) return found;
    hiprtRay traversal_ray{};
    traversal_ray.origin = ray.origin;
    traversal_ray.direction = ray.direction;
    traversal_ray.minT = 0.001f;
    traversal_ray.maxT = found ? closest_hit.distance : 1.0e30f;
    hiprtGeomTraversalClosest traversal(scene.mesh, traversal_ray);
    const hiprtHit result = traversal.getNextHit();
    if (result.hasHit()) {
        closest_hit.distance = result.t;
        closest_hit.point = ray.origin + result.t * ray.direction;
        set_face_normal(closest_hit, ray, normalize(result.normal));
        const auto& triangle = scene.triangle_materials[result.primID];
        closest_hit.object_id = triangle.object_id;
        set_material(closest_hit, triangle.material);
        found = true;
    }
    return found;
}

__device__ inline bool hit_scene(const Ray& ray, Hit& closest_hit, TraceScene scene) {
    bool found = hit_objects(ray, closest_hit, scene);
    const float closest_distance = found ? closest_hit.distance : 1.0e30f;
    Hit candidate{};
    if (hit_room(ray, closest_distance, candidate, scene.detail, scene.settings)) {
        closest_hit = candidate;
        found = true;
    }
    return found;
}

}
