#pragma once

#include "components/material/phong/model.h"
#include "geometry/hit.h"
#include "components/material/diffuse/scattering.h"
#include "math/random.h"
#include "scene/defaults.h"

namespace hip_demo::material::phong {

__device__ inline float phong_lobe_pdf(float3 axis, float3 direction) {
    return powf(fmaxf(0, dot(axis, direction)), phong_exponent) *
        ((phong_exponent + 1) / (2 * pi));
}

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
    const float lobe = phong_lobe_pdf(reflected, direction);
    return 0.5f * (diffuse + float3{1.0f, 1.0f, 1.0f} *
        (phong_specular_reflectance * cosine * lobe));
}

__device__ inline float Shader::pdf(const Ray& ray, const Hit& hit, float3 direction) const {
    if (dot(hit.normal, direction) <= 0) return 0;
    const float3 reflected = normalize(reflect(ray.direction, hit.normal));
    return 0.5f * (hip_demo::material::diffuse::Shader{}.pdf(ray, hit, direction) +
                   phong_lobe_pdf(reflected, direction));
}

__device__ inline bool Shader::scatter(Ray& ray, const Hit& hit, Random& random, float3& throughput) const {
    const Ray incoming = ray;
    if (random.next() >= 0.5f) {
        ray.direction = hip_demo::material::diffuse::sample_diffuse_direction(hit.normal, random);
    } else {
        const float3 reflected = normalize(reflect(ray.direction, hit.normal));
        ray.direction = sample_phong_lobe(reflected, random);
    }
    const float outgoing_cosine = fmaxf(0.0f, dot(ray.direction, hit.normal));
    if (outgoing_cosine == 0.0f) {
        return false;
    }
    const float density = pdf(incoming, hit, ray.direction);
    if (!(density > 0)) __builtin_trap();
    throughput = throughput * (evaluate(incoming, hit, ray.direction, outgoing_cosine) / density);
    return true;
}

}
