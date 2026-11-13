#include <hip/hip_runtime.h>

namespace {
struct TraceScene {};
}

#include "renderer_common.h"

namespace {

__device__ bool hit_objects(const Ray& ray, Hit& closest_hit, TraceScene scene) {
    (void)scene;
    return hit_direct_objects(ray, closest_hit);
}

struct HipScene {
    explicit HipScene(int) {}
    TraceScene scene() const { return {}; }
};

}

int main(int argc, char** argv) {
    return run_demo<HipScene>(argc, argv, "hip_demo.bmp");
}
