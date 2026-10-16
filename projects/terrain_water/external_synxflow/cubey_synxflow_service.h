// SPDX-License-Identifier: GPL-3.0-only
// Optional host-side service hooks for the pinned SynxFlow v1.0.1 application.
// This header is copied into the generated app directory; it does not alter
// SynxFlow library or numerical-operator sources.

#ifndef CUBEY_SYNXFLOW_SERVICE_H
#define CUBEY_SYNXFLOW_SERVICE_H

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <cuda_runtime_api.h>
#include <pybind11/pybind11.h>

#include "Scalar.h"
#include "Vector.h"

namespace cubey_synxflow_service {

inline void check_cuda(cudaError_t result, const char* operation) {
    if (result != cudaSuccess) {
        std::ostringstream message;
        message << operation << ": " << cudaGetErrorString(result);
        throw std::runtime_error(message.str());
    }
}

class CudaEvents {
  public:
    CudaEvents() : start_(nullptr), stop_(nullptr) {
        check_cuda(cudaEventCreate(&start_), "cudaEventCreate(start)");
        try {
            check_cuda(cudaEventCreate(&stop_), "cudaEventCreate(stop)");
        } catch (...) {
            (void)cudaEventDestroy(start_);
            start_ = nullptr;
            throw;
        }
    }

    CudaEvents(const CudaEvents&) = delete;
    CudaEvents& operator=(const CudaEvents&) = delete;

    ~CudaEvents() {
        if (stop_ != nullptr) {
            (void)cudaEventDestroy(stop_);
        }
        if (start_ != nullptr) {
            (void)cudaEventDestroy(start_);
        }
    }

    cudaEvent_t start() const {
        return start_;
    }
    cudaEvent_t stop() const {
        return stop_;
    }

    void close() {
        if (stop_ != nullptr) {
            const cudaError_t result = cudaEventDestroy(stop_);
            if (result == cudaSuccess) {
                stop_ = nullptr;
            }
            check_cuda(result, "cudaEventDestroy(stop)");
        }
        if (start_ != nullptr) {
            const cudaError_t result = cudaEventDestroy(start_);
            if (result == cudaSuccess) {
                start_ = nullptr;
            }
            check_cuda(result, "cudaEventDestroy(start)");
        }
    }

  private:
    cudaEvent_t start_;
    cudaEvent_t stop_;
};

struct HookResult {
    int status_code;
    bool rain_override_active;
    GC::Scalar rain_override_m_per_s;
};

struct SnapshotLease {
    SnapshotLease() : active(true), used(false) {}
    std::mutex mutex;
    bool active;
    bool used;
};

template <typename HField, typename HUField, typename ZField>
// Called synchronously at startup and after each completed native step. The
// Python hook receives (initial, physical_time_s, next_dt_s, output_due,
// snapshot_callable) and returns exactly {action, rain_override_m_per_s}.
// Snapshot is callback-scoped and one-use; zero next_dt is only valid on an
// output boundary, where SynxFlow has a stock zero-duration follow-up pass.
HookResult invoke_hook(pybind11::function& hook, bool initial, GC::Scalar physical_time_s,
                       GC::Scalar next_dt_s, bool output_due, HField& h, HUField& hU, ZField& z) {
    static_assert(sizeof(GC::Scalar) == 4, "service wire expects float32 Scalar");
    static_assert(sizeof(GC::Vector2) == 8, "service wire expects packed float32 Vector2");
    static_assert(std::is_trivially_copyable<GC::Vector2>::value,
                  "service snapshot requires trivially copyable Vector2");

    if (!std::isfinite(physical_time_s) || physical_time_s < 0.0f || !std::isfinite(next_dt_s) ||
        next_dt_s < 0.0f || (next_dt_s == 0.0f && !output_due)) {
        throw std::runtime_error("SynxFlow service hook received invalid time metadata");
    }

    const std::uint16_t endian_probe = 1;
    if (*reinterpret_cast<const std::uint8_t*>(&endian_probe) != 1) {
        throw std::runtime_error("SynxFlow service snapshots require little-endian host storage");
    }

    const std::shared_ptr<SnapshotLease> lease = std::make_shared<SnapshotLease>();
    pybind11::cpp_function snapshot([&h, &hU, &z, lease]() -> pybind11::tuple {
        std::lock_guard<std::mutex> lock(lease->mutex);
        if (!lease->active) {
            throw std::runtime_error("SynxFlow snapshot callable escaped its callback lifetime");
        }
        if (lease->used) {
            throw std::runtime_error(
                "SynxFlow snapshot callable may be used only once per callback");
        }
        lease->used = true;

        const std::size_t cells = h.data.size();
        const std::size_t max_cells = 4194304u;
        if (cells == 0 || cells > max_cells || hU.data.size() != cells || z.data.size() != cells) {
            throw std::runtime_error(
                "SynxFlow snapshot field sizes are inconsistent or out of bounds");
        }

        check_cuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize(snapshot)");
        h.data.sync();
        check_cuda(cudaGetLastError(), "h D2H snapshot");
        hU.data.sync();
        check_cuda(cudaGetLastError(), "hU D2H snapshot");
        z.data.sync();
        check_cuda(cudaGetLastError(), "z D2H snapshot");

        const std::size_t scalar_bytes = cells * sizeof(GC::Scalar);
        const std::size_t vector_bytes = cells * sizeof(GC::Vector2);
        return pybind11::make_tuple(
            pybind11::bytes(reinterpret_cast<const char*>(h.data.host_ptr()), scalar_bytes),
            pybind11::bytes(reinterpret_cast<const char*>(hU.data.host_ptr()), vector_bytes),
            pybind11::bytes(reinterpret_cast<const char*>(z.data.host_ptr()), scalar_bytes));
    });

    pybind11::object response;
    try {
        response = hook(initial, physical_time_s, next_dt_s, output_due, snapshot);
    } catch (...) {
        std::lock_guard<std::mutex> lock(lease->mutex);
        lease->active = false;
        throw;
    }
    {
        std::lock_guard<std::mutex> lock(lease->mutex);
        lease->active = false;
    }

    if (!PyDict_Check(response.ptr()) || PyDict_Size(response.ptr()) != 2) {
        throw pybind11::type_error(
            "service hook must return exactly action and rain_override_m_per_s");
    }
    PyObject* action_raw = PyDict_GetItemString(response.ptr(), "action");
    PyObject* rain_raw = PyDict_GetItemString(response.ptr(), "rain_override_m_per_s");
    if (action_raw == nullptr || rain_raw == nullptr || !PyUnicode_Check(action_raw)) {
        throw pybind11::type_error("service hook response has invalid fields");
    }

    const std::string action =
        pybind11::reinterpret_borrow<pybind11::str>(action_raw).cast<std::string>();
    int status_code = 0;
    if (action == "continue") {
        status_code = 0;
    } else if (action == "reset") {
        status_code = 1;
    } else if (action == "stop") {
        status_code = 2;
    } else {
        throw pybind11::value_error("service hook action must be continue, reset, or stop");
    }

    bool rain_override_active = false;
    GC::Scalar rain_override_m_per_s = 0.0f;
    if (rain_raw == Py_None) {
        rain_override_active = false;
        rain_override_m_per_s = 0.0f;
    } else {
        if (PyBool_Check(rain_raw) || (!PyFloat_Check(rain_raw) && !PyLong_Check(rain_raw))) {
            throw pybind11::type_error(
                "rain_override_m_per_s must be null or a finite nonnegative number");
        }
        const double value =
            pybind11::reinterpret_borrow<pybind11::object>(rain_raw).cast<double>();
        if (!std::isfinite(value) || value < 0.0 ||
            value > static_cast<double>(std::numeric_limits<GC::Scalar>::max())) {
            throw pybind11::value_error("rain_override_m_per_s must be finite and nonnegative");
        }
        rain_override_m_per_s = static_cast<GC::Scalar>(value);
        if (!std::isfinite(rain_override_m_per_s)) {
            throw pybind11::value_error("rain_override_m_per_s exceeds native Scalar range");
        }
        rain_override_active = true;
    }

    return HookResult{status_code, rain_override_active, rain_override_m_per_s};
}

} // namespace cubey_synxflow_service

#endif // CUBEY_SYNXFLOW_SERVICE_H
