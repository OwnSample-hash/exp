#include <regex>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>
#include <string.hpp>
#include <tool.hpp>

thread_local std::shared_ptr<spdlog::logger> logger = spdlog::get("webui");

const std::unordered_map<std::string, std::string> escapeCodeMap = {
    {"1", "font-bold"},
    {"3", "font-italic"},
    {"4", "font-underline"},
    {"9", "font-strikethrough"},
    {"30", "color-black"},
    {"31", "color-red"},
    {"32", "color-green"},
    {"33", "color-yellow"},
    {"34", "color-blue"},
    {"35", "color-magenta"},
    {"36", "color-cyan"},
    {"37", "color-white"},
    {"40", "bg-black"},
    {"41", "bg-red"},
    {"42", "bg-green"},
    {"43", "bg-yellow"},
    {"44", "bg-blue"},
    {"45", "bg-magenta"},
    {"46", "bg-cyan"},
    {"47", "bg-white"},
    {"90", "color-bright-black"},
    {"91", "color-bright-red"},
    {"92", "color-bright-green"},
    {"93", "color-bright-yellow"},
    {"94", "color-bright-blue"},
    {"95", "color-bright-magenta"},
    {"96", "color-bright-cyan"},
    {"97", "color-bright-white"},
    {"100", "bg-bright-black"},
    {"101", "bg-bright-red"},
    {"102", "bg-bright-green"},
    {"103", "bg-bright-yellow"},
    {"104", "bg-bright-blue"},
    {"105", "bg-bright-magenta"},
    {"106", "bg-bright-cyan"},
    {"107", "bg-bright-white"},
};

struct AnsiState {
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool strikethrough = false;
  std::string colorClass; // e.g. "color-red" or "color-256-123"
  std::string colorStyle; // e.g. "color: rgb(1,2,3)" (truecolor only)
  std::string bgClass;
  std::string bgStyle;

  void reset() {
    bold = italic = underline = strikethrough = false;
    colorClass.clear();
    colorStyle.clear();
    bgClass.clear();
    bgStyle.clear();
  }

  bool empty() const {
    return !bold && !italic && !underline && !strikethrough && colorClass.empty() && colorStyle.empty() &&
           bgClass.empty() && bgStyle.empty();
  }
};

std::string htmlEscape(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}

// Parses a single SGR parameter token. An empty token is a valid ANSI
// default (treated as 0). Anything non-numeric is a hard error.
long parseNumber(const std::string &token, const std::string &context) {
  if (token.empty()) {
    return 0;
  }
  for (char c : token) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      throw std::runtime_error("Malformed ANSI escape sequence: invalid numeric parameter '" + token + "' in " +
                               context);
    }
  }
  try {
    return std::stol(token);
  } catch (const std::exception &e) {
    throw std::runtime_error("Malformed ANSI escape sequence: could not parse '" + token + "' in " + context + " (" +
                             e.what() + ")");
  }
}

std::string buildSpanOpen(const AnsiState &state) {
  std::vector<std::string> classes;
  if (state.bold)
    classes.push_back("font-bold");
  if (state.italic)
    classes.push_back("font-italic");
  if (state.underline)
    classes.push_back("font-underline");
  if (state.strikethrough)
    classes.push_back("font-strikethrough");
  if (!state.colorClass.empty())
    classes.push_back(state.colorClass);
  if (!state.bgClass.empty())
    classes.push_back(state.bgClass);

  std::vector<std::string> styles;
  if (!state.colorStyle.empty())
    styles.push_back(state.colorStyle);
  if (!state.bgStyle.empty())
    styles.push_back(state.bgStyle);

  std::ostringstream out;
  out << "<span";
  if (!classes.empty()) {
    out << " class=\"";
    for (size_t i = 0; i < classes.size(); ++i) {
      if (i)
        out << ' ';
      out << classes[i];
    }
    out << "\"";
  }
  if (!styles.empty()) {
    out << " style=\"";
    for (size_t i = 0; i < styles.size(); ++i) {
      out << styles[i] << ';';
      if (i + 1 < styles.size())
        out << ' ';
    }
    out << "\"";
  }
  out << ">";
  return out.str();
}

// Converts a single string containing ANSI SGR escape sequences into HTML.
std::string ansiToHtml(const std::string &input) {
  static const std::regex escapeRegex("\x1b\\[([0-9;]*)m");

  std::string result;
  AnsiState state;
  bool spanOpen = false;

  auto flushText = [&](const std::string &text) {
    if (text.empty())
      return;
    if (!spanOpen && !state.empty()) {
      result += buildSpanOpen(state);
      spanOpen = true;
    }
    result += htmlEscape(text);
  };

  auto closeSpanIfOpen = [&]() {
    if (spanOpen) {
      result += "</span>";
      spanOpen = false;
    }
  };

  std::sregex_iterator it(input.begin(), input.end(), escapeRegex);
  std::sregex_iterator end;
  size_t lastPos = 0;

  for (; it != end; ++it) {
    const std::smatch &match = *it;
    size_t matchPos = static_cast<size_t>(match.position(0));
    size_t matchLen = static_cast<size_t>(match.length(0));

    // Emit any plain text that appeared before this escape sequence
    // using the state that was active up to this point.
    flushText(input.substr(lastPos, matchPos - lastPos));

    std::string paramsStr = match[1].str();
    std::vector<std::string> params;
    if (paramsStr.empty()) {
      params.push_back("0"); // "\x1b[m" == "\x1b[0m"
    } else {
      std::stringstream ss(paramsStr);
      std::string token;
      while (std::getline(ss, token, ';')) {
        params.push_back(token);
      }
      if (paramsStr.back() == ';') {
        params.push_back(""); // trailing ';' implies a default param
      }
    }

    for (size_t i = 0; i < params.size(); ++i) {
      const std::string &code = params[i];
      long codeNum = parseNumber(code, "SGR sequence '" + paramsStr + "'");

      if (codeNum == 0) {
        state.reset();
      } else if (codeNum == 21 || codeNum == 22) {
        state.bold = false;
      } else if (codeNum == 23) {
        state.italic = false;
      } else if (codeNum == 24) {
        state.underline = false;
      } else if (codeNum == 29) {
        state.strikethrough = false;
      } else if (codeNum == 39) {
        state.colorClass.clear();
        state.colorStyle.clear();
      } else if (codeNum == 49) {
        state.bgClass.clear();
        state.bgStyle.clear();
      } else if (codeNum == 38 || codeNum == 48) {
        bool isFg = (codeNum == 38);
        if (i + 1 >= params.size()) {
          throw std::runtime_error("Malformed ANSI escape sequence: '" + code + "' expects a mode parameter (5 or 2)");
        }
        long mode = parseNumber(params[i + 1], "extended color sequence");

        if (mode == 5) {
          if (i + 2 >= params.size()) {
            throw std::runtime_error("Malformed ANSI escape sequence: 256-color code missing index");
          }
          long idx = parseNumber(params[i + 2], "256-color index");
          if (idx < 0 || idx > 255) {
            throw std::runtime_error("Malformed ANSI escape sequence: 256-color index out of range: " + params[i + 2]);
          }
          if (isFg) {
            state.colorClass = "color-256-" + std::to_string(idx);
            state.colorStyle.clear();
          } else {
            state.bgClass = "bg-256-" + std::to_string(idx);
            state.bgStyle.clear();
          }
          i += 2;
        } else if (mode == 2) {
          if (i + 4 >= params.size()) {
            throw std::runtime_error("Malformed ANSI escape sequence: truecolor code missing RGB components");
          }
          long r = parseNumber(params[i + 2], "truecolor red component");
          long g = parseNumber(params[i + 3], "truecolor green component");
          long b = parseNumber(params[i + 4], "truecolor blue component");
          if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
            throw std::runtime_error("Malformed ANSI escape sequence: truecolor component out of range");
          }
          std::ostringstream styleStream;
          if (isFg) {
            styleStream << "color: rgb(" << r << "," << g << "," << b << ")";
            state.colorStyle = styleStream.str();
            state.colorClass.clear();
          } else {
            styleStream << "background-color: rgb(" << r << "," << g << "," << b << ")";
            state.bgStyle = styleStream.str();
            state.bgClass.clear();
          }
          i += 4;
        } else {
          throw std::runtime_error("Malformed ANSI escape sequence: unsupported extended color mode '" + params[i + 1] +
                                   "'");
        }
      } else {
        auto mapIt = escapeCodeMap.find(code);
        if (mapIt == escapeCodeMap.end()) {
          throw std::runtime_error("Unsupported ANSI escape code: " + code);
        }
        const std::string &cls = mapIt->second;
        if (cls == "font-bold") {
          state.bold = true;
        } else if (cls == "font-italic") {
          state.italic = true;
        } else if (cls == "font-underline") {
          state.underline = true;
        } else if (cls == "font-strikethrough") {
          state.strikethrough = true;
        } else if (cls.rfind("bg-", 0) == 0) {
          state.bgClass = cls;
          state.bgStyle.clear();
        } else {
          state.colorClass = cls;
          state.colorStyle.clear();
        }
      }
    }

    // Any change in state starts a fresh span for subsequent text.
    closeSpanIfOpen();
    lastPos = matchPos + matchLen;
  }

  flushText(input.substr(lastPos));
  closeSpanIfOpen();

  return result;
}

// Looks at response["convert"] (an array of field names), converts the
// ANSI escape codes in each of those string fields into HTML <span>
// markup, writes the result back into the same field, and finally erases
// the "convert" field. Throws on any structural or parsing error.
void webui::convert(nlohmann::json &response) {
  if (!response.is_object()) {
    throw std::runtime_error("convert: response must be a JSON object");
  }

  if (!response.contains("convert")) {
    throw std::runtime_error("convert: missing 'convert' field");
  }

  const nlohmann::json &convertField = response.at("convert");
  if (!convertField.is_array()) {
    throw std::runtime_error("convert: 'convert' field must be an array");
  }

  for (const auto &fieldNameJson : convertField) {
    if (!fieldNameJson.is_string()) {
      throw std::runtime_error("convert: entries in 'convert' must be strings");
    }
    const std::string fieldName = fieldNameJson.get<std::string>();

    if (!response.contains(fieldName)) {
      throw std::runtime_error("convert: field '" + fieldName + "' listed in 'convert' does not exist");
    }

    nlohmann::json &fieldValue = response.at(fieldName);
    if (!fieldValue.is_string()) {
      throw std::runtime_error("convert: field '" + fieldName + "' is not a string");
    }

    fieldValue = ansiToHtml(fieldValue.get<std::string>());
  }

  response.erase("convert");
}

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
