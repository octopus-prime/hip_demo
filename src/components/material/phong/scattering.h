#pragma once

#include "components/material/phong/model.h"
#include "geometry/hit.h"
#include "components/material/diffuse/scattering.h"
#include "math/random.h"
#include "scene/defaults.h"

namespace hip_demo::material::phong {

__device__ inline float3 sample_phong_lobe(float3 axis, Random& random) {
    const float3 reference = fabsf(axis.y) < 0.99f ? float3{0.0f, 1.0f, 0.0f} : float3{1.0f, 0.0f, 0.0f};
    const float3 tangent = normalize(cross(reference, axis));
    const float3 bitangent = cross(axis, tangent);
    const float cosine = powf(random.next(), 1.0f / (phong_exponent + 1.0f));
    const float sine = sqrtf(fmaxf(0.0f, 1.0f - cosine * cosine));
    const float angle = 2.0f * pi * random.next();
    return cosine * axis + sine * (cosf(angle) * tangent + sinf(angle) * bitangent);
}

__device__ inline float3 Shader::evaluate(const Ray& ray, const Hit& hit, float3 direction, float cosine) const {
    const float3 diffuse = hip_demo::material::diffuse::Shader{}.evaluate(ray, hit, direction, cosine);
    const float3 reflected = normalize(reflect(ray.direction, hit.normal));
    // This normalized lobe integrates to one even before the surface cosine.
    const float lobe = powf(fmaxf(0.0f, dot(reflected, direction)), phong_exponent) *
        ((phong_exponent + 1.0f) / (2.0f * pi));
    return 0.5f * (diffuse + float3{1.0f, 1.0f, 1.0f} *
        (phong_specular_reflectance * cosine * lobe));
}

__device__ inline bool Shader::scatter(Ray& ray, const Hit& hit, Random& random, float3& throughput) const {
    if (random.next() >= 0.5f)
        return hip_demo::material::diffuse::Shader{}.scatter(ray, hit, random, throughput);
    const float3 reflected = normalize(reflect(ray.direction, hit.normal));
    ray.direction = sample_phong_lobe(reflected, random);
    const float outgoing_cosine = fmaxf(0.0f, dot(ray.direction, hit.normal));
    if (outgoing_cosine == 0.0f) {
        return false;
    }
    // The normalized Phong lobe is its own sampling PDF.
    throughput = throughput * (phong_specular_reflectance * outgoing_cosine);
    return true;
}

}
