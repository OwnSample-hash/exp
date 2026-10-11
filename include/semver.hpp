#pragma once

#include <ranges>
#include <stdexcept>
#include <string.hpp>
#include <string>

namespace explo {

struct SemVer {
  unsigned long major;
  unsigned long minor;
  unsigned long patch;
  std::string preRelease, buildMetadata;

  SemVer(unsigned long maj, unsigned long min, unsigned long pat, const std::string_view preRel = "",
         const std::string_view buildMeta = "")
      : major(maj), minor(min), patch(pat), preRelease(preRel), buildMetadata(buildMeta) {}

  constexpr SemVer(std::string_view version) {
    size_t firstDot = version.find('.');
    size_t secondDot = version.find('.', firstDot + 1);
    size_t preReleaseStart = version.find('-', secondDot + 1);
    size_t buildMetadataStart = version.find('+', secondDot + 1);

    if (firstDot == std::string::npos || secondDot == std::string::npos) {
      throw std::invalid_argument("Invalid semantic version string: " + std::string(version));
    }

    major = std::stoul(std::string(version.substr(0, firstDot)));
    minor = std::stoul(std::string(version.substr(firstDot + 1, secondDot - firstDot - 1)));
    patch = std::stoul(std::string(version.substr(secondDot + 1)));
    if (preReleaseStart != std::string::npos) {
      preRelease = std::string(version.substr(preReleaseStart + 1, buildMetadataStart - preReleaseStart - 1));
    }
    if (buildMetadataStart != std::string::npos) {
      buildMetadata = std::string(version.substr(buildMetadataStart + 1));
    }
  }

  std::string toString() const {
    std::string version = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if (!preRelease.empty()) {
      version += "-" + preRelease;
    }
    if (!buildMetadata.empty()) {
      version += "+" + buildMetadata;
    }
    return version;
  }

  operator std::string() const { return toString(); }

  bool operator==(const SemVer &other) const {
    return major == other.major && minor == other.minor && patch == other.patch;
  }

  bool operator!=(const SemVer &other) const { return !(*this == other); }

  bool operator<(const SemVer &other) const {
    if (major != other.major)
      return major < other.major;
    if (minor != other.minor)
      return minor < other.minor;
    if (patch != other.patch)
      return patch < other.patch;
    if (preRelease.empty() && !other.preRelease.empty())
      return false; // Release versions are greater than pre-release versions
    if (!preRelease.empty() && other.preRelease.empty())
      return true; // Pre-release versions are less than release versions
    auto preReleaseParts = SplitString(preRelease, '.');
    auto otherPreReleaseParts = SplitString(other.preRelease, '.');
    for (const auto &[a, b] : std::views::zip(preReleaseParts, otherPreReleaseParts)) {
      if (a != b) {
        bool aIsNum = std::all_of(a.begin(), a.end(), ::isdigit);
        bool bIsNum = std::all_of(b.begin(), b.end(), ::isdigit);
        if (aIsNum && bIsNum) {
          return std::stoul(a) < std::stoul(b);
        }
        if (a != b)
          return a < b; // Lexicographical comparison for non-numeric identifiers
      }
    }
    return preReleaseParts.size() < otherPreReleaseParts.size();
  }

  bool operator>(const SemVer &other) const { return other < *this; }

  bool operator<=(const SemVer &other) const { return !(*this > other); }

  bool operator>=(const SemVer &other) const { return !(*this < other); }
};
} // namespace explo

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120: