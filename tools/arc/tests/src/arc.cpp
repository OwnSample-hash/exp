#include "../../include/arc.hpp"
#include <catch2/catch_test_macros.hpp>
#include <sys/stat.h>

constexpr std::string make_path(const std::string &input) {
  std::string path = __FILE__;
  size_t pos = path.find_last_of("/\\");
  if (pos != std::string::npos) {
    path = path.substr(0, pos + 1) + input;
  } else {
    path = input;
  }
  return path;
}

const std::string P_archive = make_path("test.arc");
const std::string P_cmake_file = make_path("../../CMakeLists.txt");
const std::string P_arc_file = make_path("../../arc.hpp");
const std::string P_arc_cpp_file = make_path("../../arc.cpp");

TEST_CASE("Create archive", "[arc]") {
  SECTION("Create archive") {
    arc::Arc archive;
    REQUIRE(archive.create("test.arc"));
  }

  SECTION("Add file to archive") {
    arc::Arc archive;
    INFO("Creating archive 'test.arc' and adding files...");
    REQUIRE(archive.create(P_archive));
    REQUIRE(archive.add_file(P_cmake_file));
    REQUIRE(archive.add_file(P_arc_file));
    REQUIRE(archive.add_file(P_arc_cpp_file));
  }

  SECTION("List files in archive") {
    arc::Arc archive;
    REQUIRE(archive.open(P_archive));
    REQUIRE(archive.get_file_count() == 3);
    for (uint32_t i = 0; i < archive.get_file_count(); ++i) {
      const arc::Piece &piece = archive.get_piece(i);
      INFO("File: " << piece.name << ", Size: " << piece.size << " bytes");
    }
  }

  SECTION("Extract file from archive") {
    arc::Arc archive;
    REQUIRE(archive.open(P_archive));
    for (uint32_t i = 0; i < archive.get_file_count(); ++i) {
      const arc::Piece &piece = archive.get_piece(i);
      REQUIRE(archive.extract_file(piece.name, "/tmp/" + std::string(piece.name) + "_extracted"));
      struct stat st;
      REQUIRE(stat(("/tmp/" + std::string(piece.name) + "_extracted").c_str(), &st) == 0);
      REQUIRE(st.st_size == piece.size);
      REQUIRE(remove(("/tmp/" + std::string(piece.name) + "_extracted").c_str()) == 0);
    }
  }
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
