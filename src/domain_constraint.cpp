#include "expassign/constraints/domain_constraint.hpp"

#include <algorithm>
#include <stdexcept>

#include "expassign/request.hpp"

namespace expassign {
namespace {

std::string ToLower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::vector<std::string> SplitLabels(std::string_view text) {
  std::vector<std::string> labels;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t dot = text.find('.', pos);
    size_t end = dot == std::string_view::npos ? text.size() : dot;
    labels.emplace_back(text.substr(pos, end - pos));
    if (dot == std::string_view::npos) break;
    pos = dot + 1;
  }
  return labels;
}

}  // namespace

DomainConstraint::Pattern DomainConstraint::ParsePattern(std::string_view text) {
  std::string lowered = ToLower(text);
  std::vector<std::string> labels = SplitLabels(lowered);
  if (labels.empty()) throw std::invalid_argument("empty domain pattern");

  Pattern p;
  size_t begin = 0;
  size_t end = labels.size();
  if (labels.front() == "*") {
    p.star = true;
    ++begin;
  }
  if (end > begin && labels.back() == "{tld}") {
    p.tld_wildcard = true;
    --end;
  }
  if (begin >= end) {
    throw std::invalid_argument("domain pattern '" + std::string(text) +
                                "' must contain at least one fixed label");
  }
  for (size_t i = begin; i < end; ++i) {
    const std::string& label = labels[i];
    if (label.empty() || label == "*" || label == "{tld}") {
      throw std::invalid_argument("bad label '" + label + "' in domain pattern '" +
                                  std::string(text) +
                                  "': '*' is allowed only as the leftmost label, "
                                  "'{tld}' only as the rightmost one");
    }
  }
  // suffix — от TLD к младшим.
  for (size_t i = end; i > begin; --i) {
    p.suffix.push_back(labels[i - 1]);
  }
  return p;
}

std::vector<std::string> DomainConstraint::SplitHostReversed(std::string_view host) {
  if (!host.empty() && host.back() == '.') host.remove_suffix(1);  // FQDN
  if (host.empty()) return {};
  std::vector<std::string> labels = SplitLabels(ToLower(host));
  for (const auto& label : labels) {
    if (label.empty()) return {};
  }
  std::reverse(labels.begin(), labels.end());
  return labels;
}

bool DomainConstraint::MatchHost(const Pattern& pattern,
                                 const std::vector<std::string>& host) {
  size_t idx = 0;
  if (pattern.tld_wildcard) {
    if (host.empty()) return false;
    idx = 1;  // любая метка на позиции TLD
  }
  if (host.size() < idx + pattern.suffix.size()) return false;
  for (size_t i = 0; i < pattern.suffix.size(); ++i) {
    if (host[idx + i] != pattern.suffix[i]) return false;
  }
  size_t consumed = idx + pattern.suffix.size();
  return pattern.star ? true : consumed == host.size();
}

DomainConstraint::DomainConstraint(std::string property,
                                   const std::vector<std::string>& patterns)
    : IConstraint(std::move(property)) {
  if (patterns.empty()) {
    throw std::invalid_argument("domain constraint requires at least one pattern");
  }
  patterns_.reserve(patterns.size());
  for (const auto& text : patterns) {
    patterns_.push_back(ParsePattern(text));
  }
}

bool DomainConstraint::Matches(const Request& request) const {
  auto it = request.domains.find(Property());
  if (it == request.domains.end()) return false;
  std::vector<std::string> host = SplitHostReversed(it->second);
  if (host.empty()) return false;
  for (const auto& pattern : patterns_) {
    if (MatchHost(pattern, host)) return true;
  }
  return false;
}

}  // namespace expassign
