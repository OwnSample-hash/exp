#pragma once
#include <map>
#include <rapidfuzz/fuzz.hpp>
#include <tuple>
#include <vector>

namespace explo {

std::vector<std::tuple<double, std::string, std::string>> inline extract(
    const std::string_view query, const std::map<std::string, std::vector<std::string>> &choices,
    const double score_cutoff = 0.0) {

  std::vector<std::tuple<double, std::string, std::string>> results;

  rapidfuzz::fuzz::CachedRatio<std::string::value_type> scorer(query);

  for (const auto &[toolName, tags] : choices) {
    for (const auto &tag : tags) {
      double score = scorer.similarity(tag, score_cutoff);

      if (score >= score_cutoff) {
        results.push_back(std::make_tuple(score, std::string{toolName}, std::string(tag)));
      }
    }
  }

  return results;
}

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
