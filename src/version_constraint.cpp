#include "expassign/constraints/version_constraint.hpp"

#include <stdexcept>

#include "expassign/request.hpp"

namespace expassign {
namespace {

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

std::optional<Version> ParseBound(std::string_view text, std::string_view full) {
  text = Trim(text);
  if (text.empty()) return std::nullopt;  // бесконечность
  auto v = Version::Parse(text);
  if (!v) {
    throw std::invalid_argument("bad version bound '" + std::string(text) +
                                "' in interval spec '" + std::string(full) + "'");
  }
  return v;
}

}  // namespace

bool VersionInterval::Contains(const Version& v) const {
  if (lo) {
    int c = v.Compare(*lo);
    if (c < 0 || (c == 0 && !lo_inclusive)) return false;
  }
  if (hi) {
    int c = v.Compare(*hi);
    if (c > 0 || (c == 0 && !hi_inclusive)) return false;
  }
  return true;
}

VersionConstraint::VersionConstraint(std::string property,
                                     std::vector<VersionInterval> intervals)
    : IConstraint(std::move(property)), intervals_(std::move(intervals)) {}

std::vector<VersionInterval> VersionConstraint::ParseIntervals(std::string_view spec) {
  std::vector<VersionInterval> result;
  size_t pos = 0;
  while (pos <= spec.size()) {
    size_t sep = spec.find(';', pos);
    std::string_view piece =
        Trim(spec.substr(pos, sep == std::string_view::npos ? spec.size() - pos
                                                            : sep - pos));
    pos = sep == std::string_view::npos ? spec.size() + 1 : sep + 1;
    if (piece.empty()) {
      if (sep == std::string_view::npos && result.empty() && Trim(spec).empty()) {
        throw std::invalid_argument("empty interval spec");
      }
      if (sep == std::string_view::npos) break;
      throw std::invalid_argument("empty interval in spec '" + std::string(spec) + "'");
    }
    if (piece.size() < 3 || (piece.front() != '[' && piece.front() != '(') ||
        (piece.back() != ']' && piece.back() != ')')) {
      throw std::invalid_argument("malformed interval '" + std::string(piece) + "'");
    }
    VersionInterval iv;
    iv.lo_inclusive = piece.front() == '[';
    iv.hi_inclusive = piece.back() == ']';
    std::string_view inner = piece.substr(1, piece.size() - 2);
    size_t comma = inner.find(',');
    if (comma == std::string_view::npos ||
        inner.find(',', comma + 1) != std::string_view::npos) {
      throw std::invalid_argument("interval must contain exactly one ',': '" +
                                  std::string(piece) + "'");
    }
    iv.lo = ParseBound(inner.substr(0, comma), piece);
    iv.hi = ParseBound(inner.substr(comma + 1), piece);
    result.push_back(std::move(iv));
  }
  if (result.empty()) {
    throw std::invalid_argument("empty interval spec");
  }
  return result;
}

std::shared_ptr<const VersionConstraint> VersionConstraint::Parse(
    std::string property, std::string_view spec) {
  return std::make_shared<VersionConstraint>(std::move(property),
                                             ParseIntervals(spec));
}

bool VersionConstraint::Matches(const Request& request) const {
  auto it = request.versions.find(Property());
  if (it == request.versions.end()) return false;
  for (const auto& iv : intervals_) {
    if (iv.Contains(it->second)) return true;
  }
  return false;
}

}  // namespace expassign
