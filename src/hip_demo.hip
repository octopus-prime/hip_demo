#include <hip/hip_runtime.h>

namespace {
struct TraceScene {};
}

#include "renderer_common.h"

namespace {

__device__ bool hit_objects(const Ray& ray, Hit& closest_hit, TraceScene scene) {
    bool found = false;
    float closest_distance = 1.0e30f;
    Hit candidate{};

    (void)scene;
    if (hit_sphere(ray, sphere_center, sphere_radius, closest_distance, candidate)) {
        closest_hit = candidate;
        closest_distance = candidate.distance;
        found = true;
    }
    if (hit_torus(ray, closest_distance, candidate)) {
        closest_hit = candidate;
        closest_distance = candidate.distance;
        found = true;
    }
    return found;
}

struct HipScene {
    explicit HipScene(int) {}
    TraceScene scene() const { return {}; }
};

}

int main(int argc, char** argv) {
    return run_demo<HipScene>(argc, argv, "hip_demo.bmp");
}
