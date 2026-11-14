#pragma once

#include "gpu/runtime.h"
#include "rendering/kernels.h"
#include <algorithm>
#include <iomanip>

namespace hip_demo {

struct GpuImage {
    int width, height;
    std::unique_ptr<std::uint32_t, HipBufferDeleter> pixels;
};

inline GpuImage render_image(TraceScene scene, const RenderOptions& options) {
    const int image_width = options.width;
    const int image_height = options.height;
    const int sample_count = options.sampling.maximum;
    const bool adaptive = options.sampling.adaptive;
    const std::size_t pixel_count = static_cast<std::size_t>(image_width) * image_height;
    GpuImage result{image_width, image_height, {}};
    std::uint32_t* allocation = nullptr;
    check_hip(hipMalloc(reinterpret_cast<void**>(&allocation), pixel_count * sizeof(std::uint32_t)),
              "Allocating the image on the GPU");
    result.pixels.reset(allocation);
    std::unique_ptr<float3, HipBufferDeleter> device_sums;
    std::unique_ptr<AdaptivePixel, HipBufferDeleter> adaptive_states;
    std::unique_ptr<SampleStatistics, HipBufferDeleter> device_statistics;
    if (adaptive) {
        AdaptivePixel* state_allocation = nullptr;
        check_hip(hipMalloc(reinterpret_cast<void**>(&state_allocation), pixel_count * sizeof(AdaptivePixel)),
                  "Allocating adaptive sampling state on the GPU");
        adaptive_states.reset(state_allocation);
        SampleStatistics* statistics_allocation = nullptr;
        check_hip(hipMalloc(reinterpret_cast<void**>(&statistics_allocation), sizeof(SampleStatistics)),
                  "Allocating sample statistics on the GPU");
        device_statistics.reset(statistics_allocation);
        const SampleStatistics initial{0, 0xffffffffu, 0};
        check_hip(hipMemcpy(device_statistics.get(), &initial, sizeof(initial), hipMemcpyHostToDevice),
                  "Initializing sample statistics");
    } else {
        float3* sums_allocation = nullptr;
        check_hip(hipMalloc(reinterpret_cast<void**>(&sums_allocation), pixel_count * sizeof(float3)),
                  "Allocating the sample sums on the GPU");
        device_sums.reset(sums_allocation);
    }

    const dim3 block_size{16, 16};
    const dim3 grid_size{
        static_cast<unsigned int>((image_width + block_size.x - 1) / block_size.x),
        static_cast<unsigned int>((image_height + block_size.y - 1) / block_size.y),
    };
    HipEvent start;
    HipEvent stop;
    float render_milliseconds = 0.0f;
    int last_progress = 0;
    std::cout << "Rendering: 0% (0/" << sample_count << " sample passes)" << std::flush;
    for (int first_sample = 0; first_sample < sample_count;) {
        const int sample_end = first_sample + std::min(options.sampling.batch, sample_count - first_sample);
        check_hip(hipEventRecord(start.value, nullptr), "Starting GPU batch timer");
        if (adaptive) {
            hipLaunchKernelGGL(render_adaptive_kernel, grid_size, block_size, 0, 0, result.pixels.get(),
                               adaptive_states.get(), scene, image_width, image_height,
                               first_sample, sample_end, sample_count, options.sampling);
        } else {
            hipLaunchKernelGGL(render_kernel, grid_size, block_size, 0, 0, result.pixels.get(),
                               device_sums.get(), scene, image_width, image_height,
                               first_sample, sample_end, sample_count);
        }
        check_hip(hipGetLastError(), "Launching the HIP renderer");
        if (adaptive && sample_end == sample_count) {
            hipLaunchKernelGGL(summarize_samples, dim3(static_cast<unsigned int>((pixel_count + 255) / 256)),
                               dim3(256), 0, 0, adaptive_states.get(), pixel_count, device_statistics.get());
            check_hip(hipGetLastError(), "Launching sample statistics");
        }
        check_hip(hipEventRecord(stop.value, nullptr), "Stopping GPU batch timer");
        check_hip(hipEventSynchronize(stop.value), "Waiting for the HIP render batch");
        float batch_milliseconds = 0.0f;
        check_hip(hipEventElapsedTime(&batch_milliseconds, start.value, stop.value), "Reading GPU batch time");
        render_milliseconds += batch_milliseconds;
        first_sample = sample_end;
        const int progress = static_cast<int>(100ll * sample_end / sample_count);
        if (progress != last_progress) {
            std::cout << "\rRendering: " << progress << "% (" << sample_end << '/'
                      << sample_count << " sample passes)" << std::flush;
            last_progress = progress;
        }
    }
    std::cout << "\nGPU render time: " << std::fixed << std::setprecision(3)
              << render_milliseconds / 1000.0f << " s\n";
    if (adaptive) {
        SampleStatistics statistics{};
        check_hip(hipMemcpy(&statistics, device_statistics.get(), sizeof(statistics), hipMemcpyDeviceToHost),
                  "Reading sample statistics");
        std::cout << "Samples per pixel: min " << statistics.minimum << ", average "
                  << std::setprecision(1) << static_cast<double>(statistics.total) / pixel_count
                  << ", max " << statistics.maximum << ".\n";
    }

    return result;
}

}
