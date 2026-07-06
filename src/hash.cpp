#include "expassign/hash.hpp"

#include <cstring>

namespace expassign {
namespace {

constexpr uint64_t kPrime1 = 0x9E3779B185EBCA87ULL;
constexpr uint64_t kPrime2 = 0xC2B2AE3D27D4EB4FULL;
constexpr uint64_t kPrime3 = 0x165667B19E3779F9ULL;
constexpr uint64_t kPrime4 = 0x85EBCA77C2B2AE63ULL;
constexpr uint64_t kPrime5 = 0x27D4EB2F165667C5ULL;

inline uint64_t Rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }

inline uint64_t Read64(const uint8_t* p) {
  uint64_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;  // little-endian платформы
}

inline uint32_t Read32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

inline uint64_t Round(uint64_t acc, uint64_t input) {
  acc += input * kPrime2;
  acc = Rotl(acc, 31);
  acc *= kPrime1;
  return acc;
}

inline uint64_t MergeRound(uint64_t acc, uint64_t val) {
  val = Round(0, val);
  acc ^= val;
  acc = acc * kPrime1 + kPrime4;
  return acc;
}

}  // namespace

uint64_t XXH64(const void* data, size_t len, uint64_t seed) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  const uint8_t* end = p + len;
  uint64_t h;

  if (len >= 32) {
    uint64_t v1 = seed + kPrime1 + kPrime2;
    uint64_t v2 = seed + kPrime2;
    uint64_t v3 = seed;
    uint64_t v4 = seed - kPrime1;
    do {
      v1 = Round(v1, Read64(p)); p += 8;
      v2 = Round(v2, Read64(p)); p += 8;
      v3 = Round(v3, Read64(p)); p += 8;
      v4 = Round(v4, Read64(p)); p += 8;
    } while (p + 32 <= end);
    h = Rotl(v1, 1) + Rotl(v2, 7) + Rotl(v3, 12) + Rotl(v4, 18);
    h = MergeRound(h, v1);
    h = MergeRound(h, v2);
    h = MergeRound(h, v3);
    h = MergeRound(h, v4);
  } else {
    h = seed + kPrime5;
  }

  h += static_cast<uint64_t>(len);

  while (p + 8 <= end) {
    h ^= Round(0, Read64(p));
    h = Rotl(h, 27) * kPrime1 + kPrime4;
    p += 8;
  }
  if (p + 4 <= end) {
    h ^= static_cast<uint64_t>(Read32(p)) * kPrime1;
    h = Rotl(h, 23) * kPrime2 + kPrime3;
    p += 4;
  }
  while (p < end) {
    h ^= static_cast<uint64_t>(*p) * kPrime5;
    h = Rotl(h, 11) * kPrime1;
    ++p;
  }

  h ^= h >> 33;
  h *= kPrime2;
  h ^= h >> 29;
  h *= kPrime3;
  h ^= h >> 32;
  return h;
}

}  // namespace expassign
