/**
 * @file
 * @brief String utilities.
 */

#pragma once

#include <cstring>
#include <iterator>
#include <string>

#include <iterator.hpp>

namespace explo {

/**
 * @class SplitStringYield
 * @brief A class that yields substrings of a string split by a delimiter.
 *
 */
struct SplitStringYield {

  const std::string Str;
  const char Delimiter;

  /**
   * @class iterator
   * @brief An iterator that yields substrings of a string split by a delimiter.
   *
   */
  class iterator
      : public IteratorFacade<SplitStringYield, std::forward_iterator_tag,
                              std::string> {
    std::string Current;
    const char *Index, *Begin;
    const char Delim;

  public:
    explicit iterator(const std::string current, char delimiter = '\0')
        : Current(), Delim(delimiter) {
      Index = Begin = strdup(current.c_str());
      this->operator++();
    }

    ~iterator() { free((void *)Begin); }

    bool operator==(const iterator &That) const {
      return Current == That.Current;
    }

    iterator &operator++() {
      const char *Start = Index;
      while (*Index != '\0' && *Index != Delim) {
        ++Index;
      }
      if (*Index == '\0') {
        Current = std::string(Start, Index - Start);
      } else {
        Current = std::string(Start, Index - Start);
        ++Index;
      }
      return *this;
    }
    const std::string &operator*() const { return Current; }
  };

  /**
   * @brief Get an iterator to the beginning of the split string.
   *
   * @return iterator An iterator to the beginning of the split string.
   */
  iterator begin() { return iterator(Str, Delimiter); }

  /**
   * @brief Get an iterator to the end of the split string.
   *
   * @return iterator An iterator to the end of the split string.
   */
  iterator end() { return iterator(""); }

  /**
   * @brief Construct a new Split String Yield object.
   *
   * @param s The string to split.
   * @param delimiter The delimiter to split the string by.
   */
  SplitStringYield(const std::string &s, char delimiter)
      : Str(s), Delimiter(delimiter) {}
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
