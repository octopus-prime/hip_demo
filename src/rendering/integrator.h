#pragma once

#include "gpu/traversal.h"
#include "shader/dispatch.h"
#include "light/rectangle.h"
#include "light/torus.h"

namespace hip_demo {

__device__ inline float3 trace(Ray ray, Random& random, TraceScene scene, bool* specular_primary = nullptr) {
    float3 radiance{0.0f, 0.0f, 0.0f};
    float3 throughput{1.0f, 1.0f, 1.0f};
    bool include_emission = true;

    for (int bounce = 0; bounce < maximum_bounces; ++bounce) {
        Hit hit{};
        if (!hit_scene(ray, hit, scene)) {
            break;
        }
        const float3 geometric_normal = hit.tiled_floor
            ? float3{0.0f, hit.front_face ? 1.0f : -1.0f, 0.0f} : hit.normal;

        if (bounce == 0 && specular_primary &&
            (hit.material == Material::glass || hit.material == Material::metal || hit.material == Material::phong)) {
            *specular_primary = true;
        }
        if (hit.material == Material::emissive) {
            if (include_emission && hit.front_face) {
                radiance = radiance + throughput * vector3(scene.settings->light.radiance);
            }
            break;
        } else if (hit.material == Material::metal || hit.material == Material::glass) {
            include_emission = true;
        } else {
            LightSample light{};
            const auto& source = scene.settings->light;
            if (source.torus) {
                light = sample_torus_light(random, &source);
            } else {
                light = sample_rectangle_light(random, source);
            }
            const float3 shadow_origin = hit.point + geometric_normal * 0.002f;
            const float3 to_light = light.point - shadow_origin;
            const float distance_squared = dot(to_light, to_light);
            const float distance = sqrtf(distance_squared);
            const float3 light_direction = to_light / distance;
            const float cosine = fmaxf(0.0f, dot(hit.normal, light_direction));
            const float light_cosine = fmaxf(0.0f, dot(light.normal, -light_direction));
            Hit shadow_hit{};
            constexpr float shadow_tolerance = 0.001f;
            if (cosine > 0.0f && light_cosine > 0.0f &&
                (!hit.tiled_floor || dot(geometric_normal, light_direction) > 0.0f) &&
                (!hit_scene({shadow_origin, light_direction}, shadow_hit, scene) ||
                 shadow_hit.distance >= distance - shadow_tolerance)) {
                const float3 scattering = evaluate_scattering(ray, hit, light_direction, cosine);
                radiance = radiance + throughput * scattering * vector3(source.radiance) *
                    (light_cosine * light.area_weight / distance_squared);
            }
            include_emission = false;
        }

        if (!scatter(ray, hit, random, throughput)) {
            break;
        }
        if (hit.material != Material::metal && hit.material != Material::glass &&
            hit.tiled_floor && dot(ray.direction, geometric_normal) <= 0.0f) {
            break;
        }
        ray.origin = hit.point + geometric_normal * (dot(ray.direction, geometric_normal) > 0.0f ? 0.002f : -0.002f);
    }
    return radiance;
}

}
