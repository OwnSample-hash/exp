#include <catch2/catch_test_macros.hpp>

#include <codec.hpp>

using namespace enc::codec;

TEST_CASE("xor is reversible with a repeating key", "[codec]") {
  const Bytes data = toBytes("the quick brown fox");
  const Bytes key = toBytes("k3y");
  const Bytes enc = xorBytes(data, key);
  REQUIRE(enc != data);
  REQUIRE(xorBytes(enc, key) == data);
}

TEST_CASE("add and sub are inverses", "[codec]") {
  const Bytes data = {0x00, 0x7f, 0x80, 0xff};
  REQUIRE(subBytes(addBytes(data, 0x42), 0x42) == data);
}

TEST_CASE("rot wraps bits and round-trips", "[codec]") {
  const Bytes data = {0x01, 0x80, 0xa5};
  REQUIRE(rotBytes(data, 0) == data);
  REQUIRE(rotBytes(rotBytes(data, 3), 5) == data); // 3 + 5 == 8 == identity
}

TEST_CASE("reverse round-trips", "[codec]") {
  const Bytes data = {1, 2, 3, 4, 5};
  REQUIRE(reverseBytes(reverseBytes(data)) == data);
  REQUIRE(reverseBytes(data) == Bytes({5, 4, 3, 2, 1}));
}

TEST_CASE("hex encodes and decodes", "[codec]") {
  const Bytes data = {0xde, 0xad, 0xbe, 0xef};
  REQUIRE(toHex(data) == "deadbeef");
  REQUIRE(fromHex("deadbeef") == data);
  REQUIRE(fromHex("de:ad be\nef") == data); // separators ignored
  REQUIRE_THROWS(fromHex("abc"));           // odd length
  REQUIRE_THROWS(fromHex("zz"));            // bad digit
}

TEST_CASE("base64 matches known vectors and round-trips", "[codec]") {
  REQUIRE(toBase64(toBytes("")) == "");
  REQUIRE(toBase64(toBytes("f")) == "Zg==");
  REQUIRE(toBase64(toBytes("fo")) == "Zm8=");
  REQUIRE(toBase64(toBytes("foo")) == "Zm9v");
  REQUIRE(toBase64(toBytes("foobar")) == "Zm9vYmFy");
  const Bytes data = {0x00, 0xff, 0x10, 0x20, 0x7f, 0x80};
  REQUIRE(fromBase64(toBase64(data)) == data);
}

TEST_CASE("keyToBytes distinguishes hex from literal", "[codec]") {
  REQUIRE(keyToBytes("0xdeadbeef") == Bytes({0xde, 0xad, 0xbe, 0xef}));
  REQUIRE(keyToBytes("abc") == toBytes("abc"));
}

TEST_CASE("bswap reverses bytes within each word and round-trips", "[codec]") {
  const Bytes data = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  REQUIRE(swapBytes(data, 4) == Bytes({0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55}));
  REQUIRE(swapBytes(data, 2) == Bytes({0x22, 0x11, 0x44, 0x33, 0x66, 0x55, 0x88, 0x77}));
  REQUIRE(swapBytes(swapBytes(data, 4), 4) == data); // involutive
  REQUIRE(swapBytes(data, 1) == data);
  REQUIRE_THROWS(swapBytes(data, 3)); // size not a multiple of width
  REQUIRE_THROWS(swapBytes(data, 0));
}

TEST_CASE("arbitrary-width decimal round-trips", "[codec]") {
  REQUIRE(bytesToDecimal({0x00}) == "0");
  REQUIRE(bytesToDecimal({0x01, 0x00}) == "256");
  REQUIRE(bytesToDecimal({0xff, 0xff, 0xff, 0xff}) == "4294967295");
  REQUIRE(decimalToBytes("4294967295", 4) == Bytes({0xff, 0xff, 0xff, 0xff}));
  REQUIRE(decimalToBytes("256", 4) == Bytes({0x00, 0x00, 0x01, 0x00}));
  REQUIRE_THROWS(decimalToBytes("4294967296", 4)); // overflow
  REQUIRE_THROWS(decimalToBytes("12x", 4));         // bad digit
}

TEST_CASE("IPv4 string <-> bytes <-> number", "[codec][ip]") {
  REQUIRE(ipToBytes("192.168.1.1") == Bytes({192, 168, 1, 1}));
  REQUIRE(bytesToIp({192, 168, 1, 1}) == "192.168.1.1");
  REQUIRE(ipToNumber("192.168.1.1") == "3232235777");
  REQUIRE(numberToIp("3232235777", 4) == "192.168.1.1");
  REQUIRE(ipToNumber("0.0.0.0") == "0");
  REQUIRE(numberToIp("0", 4) == "0.0.0.0");
  REQUIRE_THROWS(ipToBytes("256.0.0.1"));  // octet out of range
  REQUIRE_THROWS(ipToBytes("1.2.3"));      // too few octets
}

TEST_CASE("IPv6 string <-> bytes with :: compression", "[codec][ip]") {
  const Bytes loop = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  REQUIRE(ipToBytes("::1") == loop);
  REQUIRE(bytesToIp(loop) == "::1");
  REQUIRE(bytesToIp(ipToBytes("2001:db8::1")) == "2001:db8::1");
  REQUIRE(ipToBytes("2001:db8::1") == ipToBytes("2001:0db8:0000:0000:0000:0000:0000:0001"));
  // Full round-trip through the decimal number form.
  REQUIRE(numberToIp(ipToNumber("2001:db8::ff"), 6) == "2001:db8::ff");
  REQUIRE_THROWS(ipToBytes("2001:db8::1::2")); // two '::'
  REQUIRE_THROWS(ipToBytes("gggg::1"));        // bad hex
}

TEST_CASE("formatters produce expected shapes", "[codec]") {
  const Bytes data = {0x90, 0x41};
  REQUIRE(formatHex(data) == "9041");
  REQUIRE(formatEscaped(data) == "\\x90\\x41");
  REQUIRE(formatC(data).find("unsigned char buf[2]") != std::string::npos);
  REQUIRE(formatPython(data).find("\\x90\\x41") != std::string::npos);
}

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
