/**
 * @file codec.hpp
 * @brief Pure, dependency-free byte transforms, reversible codecs and output
 * formatters used by the encoder pipeline. Nothing in here touches the
 * filesystem, spdlog, the plugin framework or any external process, which keeps
 * it trivially unit-testable.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace enc::codec {

using Bytes = std::vector<uint8_t>;

// ── Reversible byte transforms ──────────────────────────────────────────────
// Repeating-key XOR. Involutive: xorBytes(xorBytes(d, k), k) == d.
Bytes xorBytes(const Bytes &data, const Bytes &key);
// Per-byte add / subtract modulo 256 (inverse of each other).
Bytes addBytes(const Bytes &data, uint8_t delta);
Bytes subBytes(const Bytes &data, uint8_t delta);
// Rotate every byte left by n bits (n is taken modulo 8). Inverse: rotate 8-n.
Bytes rotBytes(const Bytes &data, int n);
// Reverse the byte order of the buffer.
Bytes reverseBytes(const Bytes &data);
// Swap endianness: reverse the byte order within each group of `width` bytes.
// `width` must be >= 1 and divide data.size(). Involutive for a fixed width.
Bytes swapBytes(const Bytes &data, size_t width);

// ── Reversible encodings ────────────────────────────────────────────────────
std::string toHex(const Bytes &data, const std::string &sep = "");
Bytes fromHex(const std::string &hex); // throws std::invalid_argument on bad input
std::string toBase64(const Bytes &data);
Bytes fromBase64(const std::string &b64); // throws std::invalid_argument on bad input

// ── Arbitrary-width integers ────────────────────────────────────────────────
// Interpret `be` as a big-endian unsigned integer and render it as a decimal
// string, and the reverse into exactly `nbytes` big-endian bytes (throws on
// overflow or on a non-decimal input).
std::string bytesToDecimal(const Bytes &be);
Bytes decimalToBytes(const std::string &dec, size_t nbytes);

// ── IP addresses ────────────────────────────────────────────────────────────
// Family is detected from the text ('.' => v4, ':' => v6) or the byte length
// (4 => v4, 16 => v6). All throw std::invalid_argument on malformed input.
Bytes ipToBytes(const std::string &ip);         // "1.2.3.4" => 4 bytes (network order)
std::string bytesToIp(const Bytes &packed);     // 4 or 16 bytes => canonical string
std::string ipToNumber(const std::string &ip);  // IP string => decimal integer string
std::string numberToIp(const std::string &dec, int family); // decimal => IP string (family 4 or 6)

// ── Output formatters ───────────────────────────────────────────────────────
// Render a finished blob in a representation convenient for embedding/transport.
std::string formatRaw(const Bytes &data);     // bytes verbatim, as a std::string
std::string formatHex(const Bytes &data);     // "deadbeef"
std::string formatEscaped(const Bytes &data); // "\\x90\\x90..."
std::string formatC(const Bytes &data, const std::string &name = "buf");
std::string formatPython(const Bytes &data, const std::string &name = "buf");

// ── Helpers ─────────────────────────────────────────────────────────────────
// Convert a string buffer to bytes and back without copies of intent.
Bytes toBytes(const std::string &s);
std::string toString(const Bytes &b);

// Interpret a key/data spec: "0x.." as hex, otherwise literal UTF-8 bytes.
Bytes keyToBytes(const std::string &spec);

} // namespace enc::codec

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
