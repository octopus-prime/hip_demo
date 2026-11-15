#pragma once

#include "gpu/traversal.h"
#include "shader/dispatch.h"
#include "components/light/sampling.h"

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
        const float3 geometric_normal = hit.geometric_normal;
        const auto traits = shader::visit([] __device__ (auto value) { return value.traits; }, hit.material);

        if (bounce == 0 && specular_primary && traits.specular_primary) {
            *specular_primary = true;
        }
        if (traits.emissive) {
            if (hit.front_face && (include_emission || !hit.sampled_emitter)) {
                radiance = radiance + throughput * hit.emission;
            }
            break;
        } else if (traits.delta) {
            include_emission = true;
        } else if (scene.emitter_count != 0) {
            const LightSample light = sample_light(random, scene);
            const float3 shadow_origin = hit.point + geometric_normal * 0.002f;
            const float3 to_light = light.point - shadow_origin;
            const float distance_squared = dot(to_light, to_light);
            const float distance = sqrtf(distance_squared);
            const float3 light_direction = distance > 0 ? to_light / distance : float3{};
            const float cosine = fmaxf(0.0f, dot(hit.normal, light_direction));
            const float light_cosine = fmaxf(0.0f, dot(light.normal, -light_direction));
            Hit shadow_hit{};
            constexpr float shadow_tolerance = 0.001f;
            if (distance_squared > 0 && cosine > 0.0f && light_cosine > 0.0f &&
                (!hit.has_normal_map || dot(geometric_normal, light_direction) > 0.0f) &&
                (!hit_scene({shadow_origin, light_direction}, shadow_hit, scene) ||
                 shadow_hit.distance >= distance - shadow_tolerance)) {
                const float3 scattering = evaluate_scattering(ray, hit, light_direction, cosine);
                radiance = radiance + throughput * scattering * light.radiance *
                    (light_cosine / (light.area_pdf * distance_squared));
            }
            // NEE covers all registered emitters, not only the selected one.
            include_emission = false;
        }

        if (!scatter(ray, hit, random, throughput)) {
            break;
        }
        if (!traits.delta && hit.has_normal_map && dot(ray.direction, geometric_normal) <= 0.0f) {
            break;
        }
        ray.origin = hit.point + geometric_normal * (dot(ray.direction, geometric_normal) > 0.0f ? 0.002f : -0.002f);
    }
    return radiance;
}

}
