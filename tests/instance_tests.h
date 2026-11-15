#pragma once

#include "gpu/scene.h"
#include "rendering/integrator.h"

namespace hip_demo {
__global__ void check_instances(TraceScene scene, unsigned int count, unsigned int* failure) {
    const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    const auto& placement = scene.instances[i % count];
    const auto prototype = scene.prototypes[placement.prototype].scene;
    Random random{i * 2891336453u + 741103597u};
    const float3 origin{2 * random.next() - 1,
                        placement.prototype == 8 ? 0.5f : 2 * random.next() - 1, 3};
    const Ray local{origin, normalize(-origin)};
    const auto direction = instance::vector_to_world(placement.transform, local.direction);
    const float length = sqrtf(dot(direction, direction));
    const Ray world{instance::point_to_world(placement.transform, local.origin), direction / length};
    Hit expected{}, actual{};
    const bool reference = hit_objects(local, expected, prototype);
    Hit shading = expected;
    if (reference && placement.material_override) {
        shading.normal = shading.geometric_normal;
        shading.has_normal_map = false;
        set_material(shading, placement.material);
        apply_texture(shading, local, scene.settings);
    }
    const bool found = hit_instances(world, 6 * length, actual, scene);
    unsigned int failed = 0;
    if (found != reference || (found && actual.object_id != placement.object_id)) failed |= 1;
    // Translated FP32 rays amplify coordinate rounding near tangential intersections.
    const float origin_scale = fmaxf(1, fmaxf(fabsf(world.origin.x),
        fmaxf(fabsf(world.origin.y), fabsf(world.origin.z))));
    const float tolerance = fmaxf(0.0001f, 8 * std::numeric_limits<float>::epsilon() * origin_scale /
        fmaxf(0.01f, fabsf(dot(actual.geometric_normal, world.direction))));
    if (found && (fabsf(actual.distance - expected.distance * length) > tolerance ||
        dot(actual.normal, instance::normal_to_world(placement.transform, shading.normal)) < 0.9999f ||
        dot(actual.geometric_normal, instance::normal_to_world(placement.transform, expected.geometric_normal)) < 0.9999f ||
        actual.front_face != expected.front_face)) failed |= 2;
    if ((failed & 2) && (atomicOr(failure, 2u) & 2u) == 0)
        printf("Instance %u prototype %u: distance %.9g/%.9g, normal dot %.9g, front %d/%d\n",
               placement.object_id, placement.prototype, actual.distance, expected.distance * length,
               dot(actual.normal, instance::normal_to_world(placement.transform, shading.normal)),
               actual.front_face, expected.front_face);
    const float maximum = reference ? expected.distance * length + (i % 2 ? 0.01f : -0.01f) : 6 * length;
    if (occluded(world, maximum, scene) != (reference && i % 2 != 0)) failed |= 4;
    if (found) {
        const auto material = placement.material_override ? placement.material : GpuMaterial{};
        if (placement.material_override && actual.material != material.kind) failed |= 8;
    }
    const auto light = sample_light(random, scene);
    Hit surface{};
    const Ray incoming{light.point + 0.05f * light.normal, -light.normal};
    if (!hit_objects(incoming, surface, scene) || surface.object_id != light.object_id ||
        !surface.front_face || !surface.sampled_emitter ||
        fabsf(surface.distance - 0.05f) > 0.0001f ||
        dot(surface.emission - light.radiance, surface.emission - light.radiance) > 0.000001f) failed |= 16;
    else if (fabs(light_pdf(incoming, surface, scene) / (double(light.area_pdf) * 0.0025) - 1) > 0.003)
        failed |= 32;
    if (i == 0) {
        Hit rectangle{};
        const Ray ray{{30, 5, -2}, {0, 0, 1}};
        if (!hit_objects(ray, rectangle, scene) || fabsf(rectangle.distance - 2) > 0.00001f ||
            rectangle.geometric_normal.z > -0.9999f ||
            occluded(ray, rectangle.distance, scene) ||
            !occluded(ray, nextafterf(rectangle.distance, INFINITY), scene)) failed |= 64;
        Hit blocker{};
        const Ray blocker_ray{{36, 5, -2}, {0, 0, 1}};
        if (!hit_objects(blocker_ray, blocker, scene) || fabsf(blocker.distance - 1) > 0.00001f ||
            !occluded(blocker_ray, 2, scene) || occluded(blocker_ray, blocker.distance, scene)) failed |= 64;
    }
    atomicOr(failure, failed);
}

inline void run_instance_tests(int device) {
    auto description = load_scene_description(builtin_scene_path(false));
    description.objects.clear();
    description.settings.room.size = {80, 60, 60};
    const Material base = Phong{ConstantColor{{0.2f, 0.4f, 0.7f}}};
    CsgNode cut{Sphere{}, CsgOperation::difference,
                {CsgNode{Cylinder{{}, 1, 2}}, CsgNode{Cylinder{{}, 0.4f, 4}}}};
    Mesh crystal;
    crystal.kind = SceneMeshKind::crystal;
    description.prototypes = {
        {Sphere{{}, 1}, base}, {Ellipsoid{{}, {1, 0.8f, 0.6f}}, base},
        {Cylinder{{}, 1, 2}, base}, {Cone{{}, 1, 2}, base}, {Paraboloid{{}, 1, 2}, base},
        {Hyperboloid{{}, 0.6f, 1, 2}, base}, {Torus{{}, 1, 0.25f, 0, 1}, base},
        {Csg{cut}, base}, {hip_demo::Rectangle{{}, 2, 2}, base}, {crystal, base},
        {ExtrudedText{"C:\\Windows\\Fonts\\arial.ttf", "O", 1, 0.2f, {}}, base}
    };
    description.prototypes[0].material = Diffuse{Tiles{}};
    description.prototypes[1].material = Phong{Wood{}};
    description.prototypes[2].material = Phong{Marble{}};
    description.prototypes[3].material = Diffuse{Mandelbrot{}};
    description.prototypes[4].material = Diffuse{Julia{}};
    for (unsigned int i = 0; i < 22; ++i) {
        Instance placement;
        placement.prototype = i % 11;
        placement.transform = {{-18 + 6 * float(i % 7), 3 + 8 * float(i / 7), 0},
                               {23, 37, 41}, {1.3f, 0.6f, 0.9f}};
        if (i % 3 == 0) placement.material = Metal{ConstantColor{{0.8f, 0.6f, 0.2f}}, 0.1f};
        if (i % 3 == 1) placement.material = Glass{ConstantColor{{0.9f, 0.95f, 1}}};
        description.instances.push_back(placement);
    }
    for (const unsigned int prototype : {6u, 8u, 9u}) {
        Instance placement;
        placement.prototype = prototype;
        placement.transform = {{-8 + 8 * float(description.instances.size() - 22), 34, 0},
                               {18, 27, 43}, {1.4f, 0.7f, 0.9f}};
        placement.material = Emissive{ConstantColor{{3, 4, 5}}};
        description.instances.push_back(placement);
    }
    description.instances.push_back({8, {{30, 5, 0}, {90, 0, 0}, {2, 0.5f, 1}}, std::nullopt});
    description.instances.push_back({8, {{36, 5, 0}, {90, 0, 0}, {2, 0.5f, 1}}, std::nullopt});
    description.instances.push_back({8, {{36, 5, -1}, {90, 0, 0}, {2, 0.5f, 1}}, std::nullopt});
    description.prototypes.push_back({Box{}, base});
    description.instances.push_back({11, {{-18, 35, 0}, {23, 37, 41}, {1.3f, 0.6f, 0.9f}}, std::nullopt});
    const auto prepared = prepare_scene(description);
    if (!prepared.vertices.empty() || prepared.prototypes[9].vertices.size() != mesh_triangles.size() * 3 ||
        prepared.instances.size() != 29 || prepared.emitters.size() != 3 ||
        prepared.prototypes[9].emitter_triangles.size() != mesh_triangles.size())
        throw std::runtime_error("Instances duplicated mesh storage or lost shared light distributions");
    const HiprtScene scene(device, prepared);
    auto failure = allocate_device<unsigned int>(1, "Allocating instance test failures");
    check_hip(hipMemset(failure.get(), 0, sizeof(unsigned int)), "Clearing instance test failures");
    hipLaunchKernelGGL(check_instances, dim3(16), dim3(256), 0, 0,
                       scene.scene(), static_cast<unsigned int>(prepared.instances.size()), failure.get());
    check_hip(hipGetLastError(), "Launching instance checks");
    unsigned int result = 0;
    check_hip(hipMemcpy(&result, failure.get(), sizeof(result), hipMemcpyDeviceToHost), "Reading instance checks");
    if (result) throw std::runtime_error("Instance sharing, transform, visibility or light checks failed (flags " +
                                         std::to_string(result) + ")");
    std::cout << "Shared instances, nonuniform transforms, normals, materials, endpoint visibility and light PDFs passed.\n";
}
}
