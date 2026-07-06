#include "expassign/region_tree.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace expassign {

std::optional<RegionTree::IpBytes> RegionTree::ParseIp(std::string_view ip) {
  if (ip.empty() || ip.size() > 45) return std::nullopt;
  char buf[46];
  std::memcpy(buf, ip.data(), ip.size());
  buf[ip.size()] = '\0';

  IpBytes out{};
  in6_addr a6;
  if (inet_pton(AF_INET6, buf, &a6) == 1) {
    std::memcpy(out.data(), &a6, 16);
    return out;
  }
  in_addr a4;
  if (inet_pton(AF_INET, buf, &a4) == 1) {
    out[10] = 0xFF;
    out[11] = 0xFF;
    std::memcpy(out.data() + 12, &a4, 4);
    return out;
  }
  return std::nullopt;
}

void RegionTree::AddRegion(RegionId id, RegionId parent) {
  if (!parents_.emplace(id, parent).second) {
    throw std::invalid_argument("region " + std::to_string(id) +
                                " is added twice");
  }
  built_ = false;
}

void RegionTree::AddIpRange(std::string_view from, std::string_view to,
                            RegionId region) {
  auto f = ParseIp(from);
  auto t = ParseIp(to);
  if (!f || !t) {
    throw std::invalid_argument("bad ip in range '" + std::string(from) +
                                "'-'" + std::string(to) + "'");
  }
  if (*t < *f) {
    throw std::invalid_argument("ip range '" + std::string(from) + "'-'" +
                                std::string(to) + "' is reversed");
  }
  ranges_.push_back(IpRange{*f, *t, region});
  built_ = false;
}

void RegionTree::Build() {
  std::sort(ranges_.begin(), ranges_.end(),
            [](const IpRange& a, const IpRange& b) { return a.from < b.from; });
  for (size_t i = 1; i < ranges_.size(); ++i) {
    if (!(ranges_[i - 1].to < ranges_[i].from)) {
      throw std::invalid_argument("overlapping ip ranges");
    }
  }
  for (const auto& [id, _] : parents_) {
    RegionId cur = id;
    size_t steps = 0;
    while (true) {
      auto it = parents_.find(cur);
      if (it == parents_.end() || it->second == cur) break;
      cur = it->second;
      if (++steps > parents_.size()) {
        throw std::invalid_argument("cycle in region tree at region " +
                                    std::to_string(id));
      }
    }
  }
  built_ = true;
}

std::optional<RegionTree::RegionId> RegionTree::Parent(RegionId id) const {
  auto it = parents_.find(id);
  if (it == parents_.end() || it->second == id) return std::nullopt;
  return it->second;
}

bool RegionTree::Contains(RegionId ancestor, RegionId region) const {
  RegionId cur = region;
  for (size_t guard = 0; guard <= parents_.size(); ++guard) {
    if (cur == ancestor) return true;
    auto it = parents_.find(cur);
    if (it == parents_.end() || it->second == cur) return false;
    cur = it->second;
  }
  return false;
}

std::optional<RegionTree::RegionId> RegionTree::RegionByIp(
    std::string_view ip) const {
  if (!built_) {
    throw std::logic_error("RegionTree::Build() must be called before lookups");
  }
  auto bytes = ParseIp(ip);
  if (!bytes) return std::nullopt;
  auto it = std::upper_bound(
      ranges_.begin(), ranges_.end(), *bytes,
      [](const IpBytes& v, const IpRange& r) { return v < r.from; });
  if (it == ranges_.begin()) return std::nullopt;
  --it;
  if (*bytes < it->from || it->to < *bytes) return std::nullopt;
  return it->region;
}

}  // namespace expassign
