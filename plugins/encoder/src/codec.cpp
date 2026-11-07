#include <codec.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace enc::codec {

Bytes toBytes(const std::string &s) { return Bytes(s.begin(), s.end()); }

std::string toString(const Bytes &b) { return std::string(b.begin(), b.end()); }

Bytes xorBytes(const Bytes &data, const Bytes &key) {
  if (key.empty())
    throw std::invalid_argument("xor key must not be empty");
  Bytes out(data.size());
  for (size_t i = 0; i < data.size(); ++i)
    out[i] = static_cast<uint8_t>(data[i] ^ key[i % key.size()]);
  return out;
}

Bytes addBytes(const Bytes &data, uint8_t delta) {
  Bytes out(data.size());
  for (size_t i = 0; i < data.size(); ++i)
    out[i] = static_cast<uint8_t>(data[i] + delta);
  return out;
}

Bytes subBytes(const Bytes &data, uint8_t delta) {
  Bytes out(data.size());
  for (size_t i = 0; i < data.size(); ++i)
    out[i] = static_cast<uint8_t>(data[i] - delta);
  return out;
}

Bytes rotBytes(const Bytes &data, int n) {
  const int r = ((n % 8) + 8) % 8;
  if (r == 0)
    return data;
  Bytes out(data.size());
  for (size_t i = 0; i < data.size(); ++i) {
    const uint8_t b = data[i];
    out[i] = static_cast<uint8_t>((b << r) | (b >> (8 - r)));
  }
  return out;
}

Bytes reverseBytes(const Bytes &data) { return Bytes(data.rbegin(), data.rend()); }

Bytes swapBytes(const Bytes &data, size_t width) {
  if (width == 0)
    throw std::invalid_argument("swap width must be >= 1");
  if (data.size() % width != 0)
    throw std::invalid_argument("buffer size is not a multiple of the swap width");
  Bytes out(data.size());
  for (size_t base = 0; base < data.size(); base += width)
    for (size_t i = 0; i < width; ++i)
      out[base + i] = data[base + (width - 1 - i)];
  return out;
}

std::string toHex(const Bytes &data, const std::string &sep) {
  static const char *digits = "0123456789abcdef";
  std::string out;
  out.reserve(data.size() * 2 + (data.empty() ? 0 : (data.size() - 1) * sep.size()));
  for (size_t i = 0; i < data.size(); ++i) {
    if (i && !sep.empty())
      out += sep;
    out.push_back(digits[data[i] >> 4]);
    out.push_back(digits[data[i] & 0x0f]);
  }
  return out;
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

Bytes fromHex(const std::string &hex) {
  Bytes nibbles;
  nibbles.reserve(hex.size());
  for (char c : hex) {
    if (std::isspace(static_cast<unsigned char>(c)) || c == ':' || c == '-' || c == '_')
      continue;
    const int v = hexVal(c);
    if (v < 0)
      throw std::invalid_argument("invalid hex digit");
    nibbles.push_back(static_cast<uint8_t>(v));
  }
  if (nibbles.size() % 2 != 0)
    throw std::invalid_argument("hex input has odd number of digits");
  Bytes out(nibbles.size() / 2);
  for (size_t i = 0; i < out.size(); ++i)
    out[i] = static_cast<uint8_t>((nibbles[2 * i] << 4) | nibbles[2 * i + 1]);
  return out;
}

static const char *kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string toBase64(const Bytes &data) {
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);
  size_t i = 0;
  for (; i + 3 <= data.size(); i += 3) {
    const uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out.push_back(kB64[(n >> 18) & 0x3f]);
    out.push_back(kB64[(n >> 12) & 0x3f]);
    out.push_back(kB64[(n >> 6) & 0x3f]);
    out.push_back(kB64[n & 0x3f]);
  }
  const size_t rem = data.size() - i;
  if (rem == 1) {
    const uint32_t n = data[i] << 16;
    out.push_back(kB64[(n >> 18) & 0x3f]);
    out.push_back(kB64[(n >> 12) & 0x3f]);
    out += "==";
  } else if (rem == 2) {
    const uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
    out.push_back(kB64[(n >> 18) & 0x3f]);
    out.push_back(kB64[(n >> 12) & 0x3f]);
    out.push_back(kB64[(n >> 6) & 0x3f]);
    out.push_back('=');
  }
  return out;
}

Bytes fromBase64(const std::string &b64) {
  std::array<int, 256> rev;
  rev.fill(-1);
  for (int i = 0; i < 64; ++i)
    rev[static_cast<unsigned char>(kB64[i])] = i;

  Bytes out;
  out.reserve(b64.size() / 4 * 3);
  uint32_t buf = 0;
  int bits = 0;
  for (char c : b64) {
    if (c == '=' || std::isspace(static_cast<unsigned char>(c)))
      continue;
    const int v = rev[static_cast<unsigned char>(c)];
    if (v < 0)
      throw std::invalid_argument("invalid base64 character");
    buf = (buf << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((buf >> bits) & 0xff));
    }
  }
  return out;
}

std::string formatRaw(const Bytes &data) { return toString(data); }

std::string formatHex(const Bytes &data) { return toHex(data); }

std::string formatEscaped(const Bytes &data) {
  static const char *digits = "0123456789abcdef";
  std::string out;
  out.reserve(data.size() * 4);
  for (uint8_t b : data) {
    out += "\\x";
    out.push_back(digits[b >> 4]);
    out.push_back(digits[b & 0x0f]);
  }
  return out;
}

std::string formatC(const Bytes &data, const std::string &name) {
  std::ostringstream os;
  os << "unsigned char " << name << "[" << data.size() << "] = {";
  for (size_t i = 0; i < data.size(); ++i) {
    if (i % 12 == 0)
      os << "\n  ";
    os << "0x";
    static const char *digits = "0123456789abcdef";
    os << digits[data[i] >> 4] << digits[data[i] & 0x0f];
    if (i + 1 != data.size())
      os << ", ";
  }
  os << "\n};\n";
  return os.str();
}

std::string formatPython(const Bytes &data, const std::string &name) {
  std::ostringstream os;
  os << name << " = (\n  b\"";
  for (size_t i = 0; i < data.size(); ++i) {
    if (i && i % 16 == 0)
      os << "\"\n  b\"";
    static const char *digits = "0123456789abcdef";
    os << "\\x" << digits[data[i] >> 4] << digits[data[i] & 0x0f];
  }
  os << "\"\n)\n";
  return os.str();
}

Bytes keyToBytes(const std::string &spec) {
  if (spec.size() > 2 && spec[0] == '0' && (spec[1] == 'x' || spec[1] == 'X'))
    return fromHex(spec.substr(2));
  return toBytes(spec);
}

std::string bytesToDecimal(const Bytes &be) {
  // Repeatedly divide the big-endian magnitude by 10, collecting remainders.
  Bytes digits(be.begin(), be.end());
  std::string out;
  while (true) {
    bool nonZero = false;
    unsigned rem = 0;
    for (size_t i = 0; i < digits.size(); ++i) {
      const unsigned cur = (rem << 8) | digits[i];
      digits[i] = static_cast<uint8_t>(cur / 10);
      rem = cur % 10;
      if (digits[i])
        nonZero = true;
    }
    out.push_back(static_cast<char>('0' + rem));
    if (!nonZero)
      break;
  }
  std::reverse(out.begin(), out.end());
  return out;
}

Bytes decimalToBytes(const std::string &dec, size_t nbytes) {
  if (dec.empty())
    throw std::invalid_argument("empty decimal integer");
  Bytes out(nbytes, 0);
  for (char c : dec) {
    if (c < '0' || c > '9')
      throw std::invalid_argument("invalid decimal digit");
    unsigned carry = static_cast<unsigned>(c - '0');
    // out = out * 10 + digit, big-endian, detecting overflow past nbytes.
    for (size_t i = nbytes; i-- > 0;) {
      const unsigned cur = static_cast<unsigned>(out[i]) * 10 + carry;
      out[i] = static_cast<uint8_t>(cur & 0xff);
      carry = cur >> 8;
    }
    if (carry)
      throw std::invalid_argument("decimal value does not fit in " + std::to_string(nbytes) + " bytes");
  }
  return out;
}

Bytes ipToBytes(const std::string &ip) {
  if (ip.find(':') != std::string::npos) {
    // IPv6: split on "::" (at most once) into head and tail groups.
    const size_t dpos = ip.find("::");
    auto parseGroups = [](const std::string &s, std::vector<uint16_t> &out) {
      if (s.empty())
        return;
      size_t start = 0;
      while (start <= s.size()) {
        size_t end = s.find(':', start);
        if (end == std::string::npos)
          end = s.size();
        const std::string g = s.substr(start, end - start);
        if (g.empty() || g.size() > 4)
          throw std::invalid_argument("invalid IPv6 group");
        uint16_t v = 0;
        for (char c : g) {
          const int d = hexVal(c);
          if (d < 0)
            throw std::invalid_argument("invalid IPv6 digit");
          v = static_cast<uint16_t>((v << 4) | d);
        }
        out.push_back(v);
        start = end + 1;
      }
    };
    std::vector<uint16_t> head, tail;
    if (dpos == std::string::npos) {
      parseGroups(ip, head);
      if (head.size() != 8)
        throw std::invalid_argument("IPv6 address must have 8 groups");
    } else {
      if (ip.find("::", dpos + 1) != std::string::npos)
        throw std::invalid_argument("IPv6 address has more than one '::'");
      parseGroups(ip.substr(0, dpos), head);
      parseGroups(ip.substr(dpos + 2), tail);
      if (head.size() + tail.size() >= 8)
        throw std::invalid_argument("IPv6 '::' must compress at least one zero group");
    }
    std::vector<uint16_t> groups = head;
    groups.insert(groups.end(), 8 - head.size() - tail.size(), 0);
    groups.insert(groups.end(), tail.begin(), tail.end());
    Bytes out;
    out.reserve(16);
    for (uint16_t g : groups) {
      out.push_back(static_cast<uint8_t>(g >> 8));
      out.push_back(static_cast<uint8_t>(g & 0xff));
    }
    return out;
  }

  // IPv4: four dotted decimal octets.
  Bytes out;
  size_t start = 0;
  while (start <= ip.size()) {
    size_t end = ip.find('.', start);
    if (end == std::string::npos)
      end = ip.size();
    const std::string oct = ip.substr(start, end - start);
    if (oct.empty() || oct.size() > 3)
      throw std::invalid_argument("invalid IPv4 octet");
    int v = 0;
    for (char c : oct) {
      if (c < '0' || c > '9')
        throw std::invalid_argument("invalid IPv4 digit");
      v = v * 10 + (c - '0');
    }
    if (v > 255)
      throw std::invalid_argument("IPv4 octet out of range");
    out.push_back(static_cast<uint8_t>(v));
    start = end + 1;
  }
  if (out.size() != 4)
    throw std::invalid_argument("IPv4 address must have 4 octets");
  return out;
}

std::string bytesToIp(const Bytes &packed) {
  if (packed.size() == 4) {
    std::string out;
    for (size_t i = 0; i < 4; ++i) {
      if (i)
        out.push_back('.');
      out += std::to_string(packed[i]);
    }
    return out;
  }
  if (packed.size() == 16) {
    // Render canonical IPv6, compressing the longest run of zero groups.
    uint16_t g[8];
    for (int i = 0; i < 8; ++i)
      g[i] = static_cast<uint16_t>((packed[2 * i] << 8) | packed[2 * i + 1]);
    int bestStart = -1, bestLen = 0;
    for (int i = 0; i < 8;) {
      if (g[i] != 0) {
        ++i;
        continue;
      }
      int j = i;
      while (j < 8 && g[j] == 0)
        ++j;
      if (j - i > bestLen) {
        bestLen = j - i;
        bestStart = i;
      }
      i = j;
    }
    if (bestLen < 2)
      bestStart = -1; // only compress runs of 2+ groups
    std::ostringstream os;
    os << std::hex;
    bool needColon = false; // whether the next group needs a leading ':'
    for (int i = 0; i < 8;) {
      if (i == bestStart) {
        os << "::";
        i += bestLen;
        needColon = false; // "::" already supplies the separator
        continue;
      }
      if (needColon)
        os << ':';
      os << g[i];
      needColon = true;
      ++i;
    }
    return os.str();
  }
  throw std::invalid_argument("packed IP must be 4 or 16 bytes");
}

std::string ipToNumber(const std::string &ip) { return bytesToDecimal(ipToBytes(ip)); }

std::string numberToIp(const std::string &dec, int family) {
  const size_t nbytes = (family == 6) ? 16 : 4;
  return bytesToIp(decimalToBytes(dec, nbytes));
}

} // namespace enc::codec

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
