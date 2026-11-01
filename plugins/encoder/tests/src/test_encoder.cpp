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

TEST_CASE("formatters produce expected shapes", "[codec]") {
  const Bytes data = {0x90, 0x41};
  REQUIRE(formatHex(data) == "9041");
  REQUIRE(formatEscaped(data) == "\\x90\\x41");
  REQUIRE(formatC(data).find("unsigned char buf[2]") != std::string::npos);
  REQUIRE(formatPython(data).find("\\x90\\x41") != std::string::npos);
}

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
