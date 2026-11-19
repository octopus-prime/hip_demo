#pragma once

#include <hip/hip_runtime.h>
#include <hiprt/hiprt.h>

class Context {
public:
    explicit Context(int device = 0);
    ~Context() noexcept;

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    hiprtContext get() const noexcept { return context_; }
    unsigned int allocate_geometry_type() const;
    unsigned int geometry_type_count() const noexcept { return next_geometry_type_; }

private:
    void release() noexcept;
    hiprtContext context_ = nullptr;
    mutable unsigned int next_geometry_type_ = 1;
};
