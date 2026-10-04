#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fxblock {
inline constexpr double pi = 3.14159265358979323846;

struct Stereo { float l = 0, r = 0; };
inline float clean(float x) noexcept { return std::isfinite(x) ? x : 0.f; }
inline float tiny(float x) noexcept { return std::abs(x) < 1.e-20f ? 0.f : x; }
inline double dbToGain(double db) noexcept { return std::pow(10., db/20.); }
// A one-pole coefficient for a time constant, so that `x += k*(goal-x)` closes 63% of the gap in `seconds`.
inline double timeCoefficient(double seconds, double rate) noexcept { return 1-std::exp(-1/(seconds*rate)); }
inline double lowpassCoefficient(double hz, double rate) noexcept { return 1-std::exp(-2*pi*std::min(hz, .45*rate)/rate); }

// Moves `current` toward `goal` and lands on it exactly, so a settled value never keeps a block busy.
inline void follow(double& current, double goal, double coefficient) noexcept {
    if (std::abs(goal-current) < 1e-6) current = goal;
    else current += coefficient*(goal-current);
}

// An interpolated delay line (power-of-two ring, cubic interpolation).
struct Line {
    std::vector<float> data;
    size_t mask = 0, write = 0;
    void prepare(size_t length) { size_t n = 16; while (n < length+4) n <<= 1; data.assign(n, 0.f); mask = n-1; write = 0; }
    void clear() noexcept { std::fill(data.begin(), data.end(), 0.f); write = 0; }
    void push(float x) noexcept { data[write++ & mask] = x; }
    // The sample `delay` samples ago (1 is the last one pushed), linear between the two around it.
    float readLinear(double delay) const noexcept {
        const double position = static_cast<double>(write)-delay;
        const double floor = std::floor(position);
        const size_t i = static_cast<size_t>(static_cast<int64_t>(floor));
        const float f = static_cast<float>(position-floor);
        const float a = data[i & mask], b = data[(i+1) & mask];
        return a+(b-a)*f;
    }
    // The sample `delay` samples ago, Catmull-Rom between the four around it. The newest sample the curve
    // touches is two ahead of the position, so `delay` is held at 3 or more.
    float read(double delay) const noexcept {
        const double position = static_cast<double>(write)-std::max(delay, 3.);
        const double floor = std::floor(position);
        const size_t i = static_cast<size_t>(static_cast<int64_t>(floor));
        const float f = static_cast<float>(position-floor);
        const float p0 = data[(i-1) & mask], p1 = data[i & mask], p2 = data[(i+1) & mask], p3 = data[(i+2) & mask];
        const float c1 = .5f*(p2-p0), c2 = p0-2.5f*p1+2.f*p2-.5f*p3, c3 = .5f*(p3-p0)+1.5f*(p1-p2);
        return ((c3*f+c2)*f+c1)*f+p1;
    }
};

struct Rng {
    uint32_t state = 0x6d2b79f5;
    float next() noexcept { // [0, 1)
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return static_cast<float>(state >> 8) * (1.f / 16777216.f);
    }
    float bipolar() noexcept { return next() * 2.f - 1.f; }
};

// Single-producer, single-consumer triple buffer: the producer always writes a private slot and
// publishes it; the consumer always reads the newest published slot. Neither side ever waits.
template <class T>
class TripleBuffer {
public:
    T& writeSlot() noexcept { return slots_[back_]; }
    void publish() noexcept {
        back_ = static_cast<uint8_t>(middle_.exchange(static_cast<uint8_t>(back_ | dirty), std::memory_order_acq_rel) & 3);
    }
    // Returns true when a newer value than the last call was available.
    bool acquire() noexcept {
        if (!(middle_.load(std::memory_order_acquire) & dirty)) return false;
        front_ = static_cast<uint8_t>(middle_.exchange(front_, std::memory_order_acq_rel) & 3);
        return true;
    }
    const T& readSlot() const noexcept { return slots_[front_]; }
private:
    static constexpr uint8_t dirty = 4;
    std::array<T, 3> slots_ {};
    uint8_t back_ = 0, front_ = 1;
    std::atomic<uint8_t> middle_ {2};
};
} // namespace fxblock
