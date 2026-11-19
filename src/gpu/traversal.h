#pragma once

#include "gpu/scene_view.h"
#include "components/room/intersection.h"
#include "components/room/shading.h"

namespace hip_demo {

__device__ inline bool hit_room(const Ray& ray, float maximum_distance, Hit& hit, bool detail = false,
                                const GpuSceneSettings* settings = nullptr) {
    int wall = 0;
    if (!intersect_room(ray, maximum_distance, hit, wall, settings)) {
        return false;
    }
    shade_room(hit, ray, wall, detail, settings);
    return true;
}

__device__ inline void shade_custom_hit(const Ray& ray, const hiprtHit& result, Hit& candidate, TraceScene scene) {
    candidate = {};
    candidate.distance = result.t;
    candidate.point = ray.origin + result.t * ray.direction;
    set_face_normal(candidate, ray, result.normal);
    if (result.primID < scene.primitive_count) {
        candidate.object_id = result.primID;
        set_material(candidate, scene.primitives[result.primID].material);
    } else if (result.primID - scene.primitive_count < scene.csg_count) {
        candidate.object_id = result.primID;
        candidate.csg_leaf = __float_as_uint(result.uv.x);
        set_material(candidate, scene.csg_materials ? scene.csg_materials[candidate.csg_leaf] :
                     scene.csgs[result.primID - scene.primitive_count].material);
    } else {
        const auto& glyph = scene.glyphs[result.primID - scene.primitive_count - scene.csg_count];
        candidate.object_id = glyph.object_id;
        set_material(candidate, glyph.material);
    }
    apply_texture(candidate, ray, scene.settings);
}

__device__ inline void shade_mesh_hit(const Ray& ray, const hiprtHit& result, Hit& hit, TraceScene scene) {
    hit = {};
    hit.distance = result.t;
    hit.point = ray.origin + result.t * ray.direction;
    set_face_normal(hit, ray, normalize(result.normal));
    const auto& triangle = scene.triangle_materials[result.primID];
    hit.object_id = triangle.object_id;
    set_material(hit, triangle.material);
    apply_texture(hit, ray, scene.settings);
}

__device__ inline bool hit_custom_objects(const Ray& ray, Hit& closest_hit, TraceScene scene) {
    if (!scene.geometry) return false;
    hiprtRay traversal_ray{};
    traversal_ray.origin = ray.origin;
    traversal_ray.direction = ray.direction;
    traversal_ray.minT = 0.001f;
    traversal_ray.maxT = 1.0e30f;
    hiprtGeomCustomTraversalClosest traversal(
        scene.geometry, traversal_ray, hiprtTraversalHintDefault, nullptr, scene.functions);
    const auto result = traversal.getNextHit();
    if (!result.hasHit()) return false;
    shade_custom_hit(ray, result, closest_hit, scene);
    return true;
}

__device__ inline void shade_instance_hit(const Ray& ray, const hiprtHit& result, Hit& hit, TraceScene scene) {
    const auto& placement = scene.instances[result.instanceID];
    const auto prototype = scene.prototypes[placement.prototype].scene;
    GpuMaterial material{};
    if (placement.material_override) material = placement.material;
    else if (prototype.mesh) material = prototype.triangle_materials[result.primID].material;
    else if (result.primID < prototype.primitive_count) material = prototype.primitives[result.primID].material;
    else if (result.primID - prototype.primitive_count < prototype.csg_count)
        material = prototype.csg_materials ? prototype.csg_materials[__float_as_uint(result.uv.x)] :
                   prototype.csgs[result.primID - prototype.primitive_count].material;
    else material = prototype.glyphs[result.primID - prototype.primitive_count - prototype.csg_count].material;
    material.sampled_emitter = material.kind == ShaderKind::emissive && prototype.emitter_count != 0;
    const auto& transform = placement.transform;
    const Ray local_ray{instance::point_to_local(transform, ray.origin),
                         instance::vector_to_local(transform, ray.direction)};
    hit = {};
    if (result.primID >= prototype.primitive_count &&
        result.primID - prototype.primitive_count < prototype.csg_count)
        hit.csg_leaf = __float_as_uint(result.uv.x);
    hit.distance = result.t;
    hit.point = local_ray.origin + result.t * local_ray.direction;
    set_face_normal(hit, local_ray, instance::normalized_normal(result.normal));
    set_material(hit, material);
    apply_texture(hit, local_ray, scene.settings);
    hit.point = ray.origin + result.t * ray.direction;
    hit.normal = instance::normal_to_world(transform, hit.normal);
    hit.geometric_normal = instance::normal_to_world(transform, hit.geometric_normal);
    hit.object_id = placement.object_id;
}

__device__ inline bool hit_instances(const Ray& ray, float maximum, Hit& hit, TraceScene scene) {
    if (!scene.instance_scene) return false;
    hiprtRay traversal_ray{};
    traversal_ray.origin = ray.origin;
    traversal_ray.direction = ray.direction;
    traversal_ray.minT = 0.001f;
    traversal_ray.maxT = maximum;
    hiprtSceneTraversalClosest traversal(scene.instance_scene, traversal_ray, hiprtFullRayMask,
                                         hiprtTraversalHintDefault, nullptr, scene.functions);
    const auto result = traversal.getNextHit();
    if (!result.hasHit()) return false;
    shade_instance_hit(ray, result, hit, scene);
    return true;
}

template<bool Instanced>
__device__ inline bool hit_unified_objects(const Ray& ray, Hit& hit, TraceScene scene) {
    hiprtRay traversal_ray{};
    traversal_ray.origin = ray.origin;
    traversal_ray.direction = ray.direction;
    traversal_ray.minT = 0.001f;
    traversal_ray.maxT = 1.0e30f;
    hiprtSceneTraversalClosest traversal(scene.unified_scene, traversal_ray, hiprtFullRayMask,
                                         hiprtTraversalHintDefault, nullptr, scene.functions);
    auto result = traversal.getNextHit();
    if (!result.hasHit()) return false;
    if (result.instanceID == scene.unified_custom) {
        shade_custom_hit(ray, result, hit, scene);
    } else if (result.instanceID == scene.unified_mesh) {
        shade_mesh_hit(ray, result, hit, scene);
    } else {
        if constexpr (Instanced) {
            result.instanceID -= scene.unified_instance_offset;
            shade_instance_hit(ray, result, hit, scene);
        } else {
            __builtin_trap();
        }
    }
    return true;
}

template<bool Instanced = true, bool Unified = false>
__device__ inline bool hit_objects(const Ray& ray, Hit& closest_hit, TraceScene scene) {
    if constexpr (Unified)
        return scene.unified_scene && hit_unified_objects<Instanced>(ray, closest_hit, scene);
    bool found = hit_custom_objects(ray, closest_hit, scene);
    if (scene.mesh) {
        hiprtRay traversal_ray{};
        traversal_ray.origin = ray.origin;
        traversal_ray.direction = ray.direction;
        traversal_ray.minT = 0.001f;
        traversal_ray.maxT = found ? closest_hit.distance : 1.0e30f;
        hiprtGeomTraversalClosest traversal(scene.mesh, traversal_ray);
        const hiprtHit result = traversal.getNextHit();
        if (result.hasHit()) {
            shade_mesh_hit(ray, result, closest_hit, scene);
            found = true;
        }
    }
    if constexpr (Instanced) {
        Hit candidate{};
        if (scene.instance_scene && hit_instances(ray, found ? closest_hit.distance : 1.0e30f, candidate, scene)) {
            closest_hit = candidate;
            found = true;
        }
    }
    return found;
}

template<bool Instanced = true, bool Unified = false>
__device__ inline bool hit_scene(const Ray& ray, Hit& closest_hit, TraceScene scene) {
    bool found = hit_objects<Instanced, Unified>(ray, closest_hit, scene);
    const float closest_distance = found ? closest_hit.distance : 1.0e30f;
    Hit candidate{};
    if (hit_room(ray, closest_distance, candidate, false, scene.settings)) {
        closest_hit = candidate;
        found = true;
    }
    return found;
}

template<bool Instanced = true, bool Unified = false>
__device__ inline bool occluded(const Ray& ray, float maximum_distance, TraceScene scene) {
    if (maximum_distance <= 0.001f) return false;
    Hit room_hit{};
    int wall = 0;
    if (intersect_room(ray, maximum_distance, room_hit, wall, scene.settings)) return true;
    hiprtRay traversal_ray{};
    traversal_ray.origin = ray.origin;
    traversal_ray.direction = ray.direction;
    traversal_ray.minT = 0.001f;
    traversal_ray.maxT = maximum_distance;
    if constexpr (Unified) {
        if (scene.unified_scene) {
            hiprtSceneTraversalAnyHit traversal(scene.unified_scene, traversal_ray, hiprtFullRayMask,
                                                 hiprtTraversalHintDefault, nullptr, scene.functions);
            for (auto hit = traversal.getNextHit(); hit.hasHit(); hit = traversal.getNextHit()) {
                if (hit.t < maximum_distance || hit.instanceID == scene.unified_custom) return true;
            }
        }
        return false;
    }
    if (scene.geometry) {
        hiprtGeomCustomTraversalAnyHit traversal(
            scene.geometry, traversal_ray, hiprtTraversalHintDefault, nullptr, scene.functions);
        if (traversal.getNextHit().hasHit()) return true;
    }
    if (scene.mesh) {
        // HIP RT triangles accept maxT inclusively; shadow endpoints must not block.
        traversal_ray.maxT = nextafterf(maximum_distance, -INFINITY);
        hiprtGeomTraversalAnyHit traversal(scene.mesh, traversal_ray);
        const auto hit = traversal.getNextHit();
        if (hit.hasHit() && hit.t < maximum_distance) return true;
    }
    if constexpr (Instanced) {
        if (scene.instance_scene) {
            traversal_ray.maxT = maximum_distance;
            hiprtSceneTraversalAnyHit traversal(scene.instance_scene, traversal_ray, hiprtFullRayMask,
                hiprtTraversalHintDefault, nullptr, scene.functions);
            // Resume past inclusive endpoint hits instead of hiding nearer blockers.
            for (auto hit = traversal.getNextHit(); hit.hasHit(); hit = traversal.getNextHit())
                if (hit.t < maximum_distance) return true;
        }
    }
    return false;
}

}
