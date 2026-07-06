#include "expassign/version.hpp"

#include <limits>

namespace expassign {

std::optional<Version> Version::Parse(std::string_view text) {
  if (text.empty()) return std::nullopt;
  std::vector<uint32_t> parts;
  uint64_t current = 0;
  size_t digits = 0;
  for (size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '.') {
      if (digits == 0) return std::nullopt;  // пустой компонент
      parts.push_back(static_cast<uint32_t>(current));
      current = 0;
      digits = 0;
      continue;
    }
    char c = text[i];
    if (c < '0' || c > '9') return std::nullopt;
    current = current * 10 + static_cast<uint64_t>(c - '0');
    if (current > std::numeric_limits<uint32_t>::max()) return std::nullopt;
    ++digits;
  }
  return Version(std::move(parts));
}

std::string Version::ToString() const {
  std::string out;
  for (size_t i = 0; i < parts_.size(); ++i) {
    if (i) out += '.';
    out += std::to_string(parts_[i]);
  }
  return out;
}

int Version::Compare(const Version& other) const {
  size_t n = std::max(parts_.size(), other.parts_.size());
  for (size_t i = 0; i < n; ++i) {
    uint32_t a = i < parts_.size() ? parts_[i] : 0;
    uint32_t b = i < other.parts_.size() ? other.parts_[i] : 0;
    if (a != b) return a < b ? -1 : 1;
  }
  return 0;
}

}  // namespace expassign
