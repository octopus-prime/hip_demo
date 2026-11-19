#pragma once

#include "context.h"
#include "error.h"

#include <iostream>

class FunctionTable {
public:
    FunctionTable(const Context& context, unsigned int geometry_types) : context_(context)
    {
        check_hiprt(hiprtCreateFuncTable(context_.get(), geometry_types, 1, table_),
                    "Creating geometry function table");
    }

    ~FunctionTable() noexcept
    {
        const auto result = hiprtDestroyFuncTable(context_.get(), table_);
        if (result != hiprtSuccess) {
            std::cerr << "Destroying geometry function table: HIP RT error " << result << '\n';
        }
    }

    FunctionTable(const FunctionTable&) = delete;
    FunctionTable& operator=(const FunctionTable&) = delete;

    void set(unsigned int geometry_type, const void* data)
    {
        hiprtFuncDataSet entry{};
        entry.intersectFuncData = data;
        check_hiprt(hiprtSetFuncTable(context_.get(), table_, geometry_type, 0, entry),
                    "Setting geometry intersection data");
    }

    hiprtFuncTable get() const noexcept { return table_; }

private:
    const Context& context_;
    hiprtFuncTable table_ = nullptr;
};
