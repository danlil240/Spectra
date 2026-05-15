#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace spectra
{

// ─── Publisher ───────────────────────────────────────────────────────────────
//
// ROS-style topic publisher. Sends named data samples to the Spectra backend
// (spectra-backend daemon) over the existing IPC socket. Works whether or not
// a Spectra UI window is open: when the UI later opens (or is already open),
// the Topics panel shows this topic and a user can drag it onto a plot to
// subscribe live.
//
// Sample layouts:
//   Kind::Scalar2D: pairs (x, y)         — 2 doubles per sample
//   Kind::Scalar3D: triples (x, y, z)    — 3 doubles per sample
//
// Thread-safety: a Publisher is owned by one thread; methods are not safe to
// call concurrently from multiple threads on the same instance.
class Publisher
{
   public:
    enum class Kind : uint8_t
    {
        Scalar2D = 0,
        Scalar3D = 1,
    };

    // Configuration used when creating a publisher.
    struct Options
    {
        // Override the daemon socket path. If empty, uses $SPECTRA_SOCKET or
        // the platform default.
        std::string socket_path;
        // Topic kind (data layout).
        Kind kind = Kind::Scalar2D;
        // Optional unit string (e.g. "m/s^2"). Free-form; shown in the UI.
        std::string unit;
        // Ring buffer capacity in samples retained at the daemon.
        uint32_t ring_capacity = 4096;
    };

    Publisher() = default;
    ~Publisher();

    Publisher(const Publisher&)            = delete;
    Publisher& operator=(const Publisher&) = delete;
    Publisher(Publisher&& other) noexcept;
    Publisher& operator=(Publisher&& other) noexcept;

    // Create and connect a publisher for the given topic name.
    // Returns nullptr on connection or declaration failure.
    static std::unique_ptr<Publisher> create(std::string_view name);
    static std::unique_ptr<Publisher> create(std::string_view name, const Options& opts);

    // Push a single 2D sample. No-op if kind != Scalar2D.
    bool publish(double x, double y);

    // Push a single 3D sample. No-op if kind != Scalar3D.
    bool publish(double x, double y, double z);

    // Push a batch of pre-interleaved samples (layout matches kind).
    bool publish(std::span<const double> interleaved);

    // True if connected to the daemon.
    bool is_connected() const noexcept;

    // Topic name as declared.
    std::string_view name() const noexcept;

    // Topic kind.
    Kind kind() const noexcept;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}   // namespace spectra
