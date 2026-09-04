#pragma once

#include <cstdint>

namespace route {

/// SplitMix64, the fixed 64-bit generator published at
/// https://prng.di.unimi.it/splitmix64.c (Steele, Lea and Flood, 2014).
///
/// The algorithm is written out below in full: the three constants and the
/// shift amounts are the whole specification, so one seed means one stream on
/// every platform and in every standard library. That is why neither std::rand
/// nor the std::uniform_*_distribution templates are used here -- the standard
/// pins down the engines but not the distributions, so their output legitimately
/// differs between implementations and a "deterministic" generator built on them
/// would only be deterministic on one machine.
class SplitMix64 {
 public:
  explicit constexpr SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}

  [[nodiscard]] constexpr std::uint64_t next() noexcept {
    state_ += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  /// Uniform double in [0, 1).
  ///
  /// Built from the top 53 bits, so every result is an exact multiple of
  /// 2^-53 and the mapping from bits to double introduces no rounding.
  [[nodiscard]] constexpr double next_unit() noexcept {
    return static_cast<double>(next() >> 11) * 0x1.0p-53;
  }

  /// Uniform integer in [0, bound), by rejection over a power-of-two mask.
  ///
  /// Unbiased, unlike `next() % bound`, and each attempt succeeds with
  /// probability greater than 1/2. `bound` must be positive.
  [[nodiscard]] constexpr std::uint64_t next_below(std::uint64_t bound) noexcept {
    std::uint64_t mask = bound - 1;
    mask |= mask >> 1;
    mask |= mask >> 2;
    mask |= mask >> 4;
    mask |= mask >> 8;
    mask |= mask >> 16;
    mask |= mask >> 32;
    while (true) {
      const std::uint64_t candidate = next() & mask;
      if (candidate < bound) {
        return candidate;
      }
    }
  }

 private:
  std::uint64_t state_;
};

}  // namespace route
