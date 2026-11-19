#include "context.h"
#include "error.h"

#include <iostream>
#include <limits>

Context::Context(int device)
{
    int device_count = 0;
    check_hip(hipGetDeviceCount(&device_count), "Finding GPUs");
    if (device_count == 0) {
        throw std::runtime_error("No HIP-compatible GPU found.");
    }

    if (device < 0 || device >= device_count) {
        throw std::runtime_error("HIP device index is out of range.");
    }
    check_hip(hipSetDevice(device), "Selecting GPU");

    hipDeviceProp_t properties{};
    check_hip(hipGetDeviceProperties(&properties, device), "Reading GPU properties");
    std::cout << "GPU " << device << ": " << properties.name << '\n'
              << "Architecture: " << properties.gcnArchName << '\n'
              << "HIP RT: " << HIPRT_MAJOR_VERSION << '.' << HIPRT_MINOR_VERSION
              << '.' << HIPRT_PATCH_VERSION << '\n';

    // HIP RT needs the current HIP context handle; HIP 7.1 deprecates this getter.
    hipCtx_t hip_context = nullptr;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    check_hip(hipCtxGetCurrent(&hip_context), "Getting HIP context");
#pragma clang diagnostic pop
    if (!hip_context) {
        throw std::runtime_error("No current HIP context.");
    }

    hiprtContextCreationInput input{};
    input.ctxt = hip_context;
    input.device = device;
    input.deviceType = hiprtDeviceAMD;

    check_hiprt(hiprtCreateContext(HIPRT_API_VERSION, input, context_), "Creating HIP RT context");
}

unsigned int Context::allocate_geometry_type() const
{
    if (next_geometry_type_ == std::numeric_limits<unsigned int>::max()) {
        throw std::overflow_error("Too many custom geometry types.");
    }
    return next_geometry_type_++;
}

Context::~Context() noexcept
{
    release();
}

void Context::release() noexcept
{
    const hiprtError result = hiprtDestroyContext(context_);
    if (result != hiprtSuccess) {
        std::cerr << "Destroying HIP RT context: HIP RT error " << result << '\n';
    } else {
        std::cout << "HIP RT context destroyed.\n";
    }
}
