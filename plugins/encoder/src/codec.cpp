#include <codec.hpp>

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

} // namespace enc::codec

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
