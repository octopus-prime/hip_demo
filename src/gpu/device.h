#pragma once

#include "gpu/runtime.h"
#include "hip_demo_architectures.h"

namespace hip_demo {

inline bool matches_architecture(std::string_view target, std::string_view device) {
    const auto target_separator = target.find(':');
    if (target.substr(0, target_separator) != device.substr(0, device.find(':'))) {
        return false;
    }
    if (target_separator == std::string_view::npos) {
        return true;
    }
    target.remove_prefix(target_separator + 1);
    while (!target.empty()) {
        const auto separator = target.find(':');
        const auto feature = target.substr(0, separator);
        std::string_view device_features = device;
        bool found = false;
        while (device_features.find(':') != std::string_view::npos) {
            device_features.remove_prefix(device_features.find(':') + 1);
            if (device_features.substr(0, device_features.find(':')) == feature) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        target.remove_prefix(separator + 1);
    }
    return true;
}

struct SelectedDevice {
    int id;
    hipDeviceProp_t properties;
};

inline SelectedDevice select_device(int requested_device = -1) {
    int device_count = 0;
    check_hip(hipGetDeviceCount(&device_count), "Detecting HIP devices");
    if (device_count == 0) {
        throw std::runtime_error("No HIP-compatible GPU was found.");
    }
    if (requested_device < -1 || requested_device >= device_count)
        throw std::runtime_error("Requested HIP device index is out of range.");
    int selected_device = -1;
    hipDeviceProp_t device_properties{};
    for (int device = 0; device < device_count; ++device) {
        if (requested_device >= 0 && device != requested_device) continue;
        hipDeviceProp_t candidate{};
        check_hip(hipGetDeviceProperties(&candidate, device), "Reading HIP device properties");
        std::string_view compiled_architectures(HIP_DEMO_ARCHITECTURES);
        bool supported = false;
        while (!compiled_architectures.empty()) {
            const auto separator = compiled_architectures.find(';');
            const auto target = compiled_architectures.substr(0, separator);
            if (matches_architecture(target, candidate.gcnArchName)) {
                supported = true;
                break;
            }
            if (separator == std::string_view::npos) {
                break;
            }
            compiled_architectures.remove_prefix(separator + 1);
        }
        if (supported && (selected_device < 0 || (device_properties.integrated && !candidate.integrated))) {
            selected_device = device;
            device_properties = candidate;
        }
    }
    if (selected_device < 0) {
        if (requested_device >= 0)
            throw std::runtime_error("Requested HIP device does not match the compiled GPU architectures.");
        throw std::runtime_error(
            "No HIP device matches the compiled GPU architectures (" HIP_DEMO_ARCHITECTURES
            "). Reconfigure with HIP_ARCHITECTURE for an available GPU.");
    }
    check_hip(hipSetDevice(selected_device), "Selecting HIP device");
    return {selected_device, device_properties};
}

}
