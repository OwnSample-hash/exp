#include <arc.hpp>
#include <cstring>
#include <filesystem>
#include <fuse3/fuse.h>
#include <iostream>

namespace fs = std::filesystem;
using namespace arc;

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0]
              << " <mode;c/x/l/m/t/v> <archive_file> <c:files_to_add/x:output_dir/m:path_to_mount> <m:fuse_opts>"
              << '\n';
    return 1;
  }

  if (argv[1][0] == 'c') {
    std::string archive_file = argv[2];
    arc::Arc archive;

    if (!archive.create(archive_file)) {
      std::cerr << "Failed to create archive: " << archive_file << '\n';
      return 1;
    }

    for (int i = 3; i < argc; ++i) {
      LOG_DEBUG("Adding file/directory: %s", argv[i]);
      if (fs::is_directory(argv[i])) {
        if (!archive.add_dir(argv[i])) {
          std::cerr << "Failed to add directory: " << argv[i] << '\n';
          return 1;
        }
      } else {
        if (!archive.add_file(argv[i])) {
          std::cerr << "Failed to add file: " << argv[i] << '\n';
          return 1;
        }
      }
    }
#ifndef NDEBUG
    archive.debug_print();
#endif

    return 0;
  } else if (argv[1][0] == 'x') {

    std::string archive_file = argv[2];
    std::string output_dir = (argc > 3) ? argv[3] : ".";
    arc::Arc archive;

    if (!archive.open(archive_file)) {
      std::cerr << "Failed to open archive: " << archive_file << '\n';
      return 1;
    }

    // Example usage: list files in the archive
    for (uint32_t i = 0; i < archive.get_file_count(); ++i) {
      const arc::Piece &piece = archive.get_piece(i);
      std::cout << "File: " << piece.name << ", Size: " << piece.size << " bytes" << '\n';
      std::cout << "Extracting file: " << piece.name << '\n';
      fs::path output_path = fs::path(output_dir) / piece.name;
      if (!archive.extract_file(piece.name, output_path.string())) {
        std::cerr << "Failed to extract file: " << piece.name << '\n';
        return 1;
      }
    }

#ifndef NDEBUG
    archive.debug_print();
#endif

    return 0;
  } else if (argv[1][0] == 'l') {

    std::string archive_file = argv[2];
    arc::Arc archive;

    if (!archive.open(archive_file)) {
      std::cerr << "Failed to open archive: " << archive_file << '\n';
      return 1;
    }

    // Example usage: list files in the archive
    for (uint32_t i = 0; i < archive.get_file_count(); ++i) {
      const arc::Piece &piece = archive.get_piece(i);
      std::cout << "File: " << piece.name << ", Size: " << piece.size << " bytes" << '\n';
    }
  } else if (argv[1][0] == 'v') {
    std::cout << argv[0] << " version " << arc::VERSION << " fuse version: " << FUSE_VERSION << '\n';
  } else if (argv[1][0] == 'm') {
    std::string archive_file = argv[2];
    arc::Arc archive;

    char **fargv = (char **)calloc(sizeof(char *), argc - 2);
    char *app = fargv[0] = strdup(argv[0]);
    for (int i = 3; i < argc; ++i) {
      fargv[i - 3] = strdup(argv[i]);
    }
#ifndef NDEBUG
    for (int i = 0; i < argc - 3; ++i) {
      LOG_DEBUG("fargv[%d] = %s", i, fargv[i]);
    }
#endif

    if (!archive.open(archive_file)) {
      std::cerr << "Failed to open archive: " << archive_file << '\n';
      return 1;
    }

    if (!archive.mount_archive(argv[3], argc - 3, fargv)) {
      std::cerr << "Failed to mount archive: " << archive_file << '\n';
      return 1;
    }

    free(app);
  } else if (argv[1][0] == 't') {
    std::string archive_file = argv[2];
    arc::Arc archive;

    if (!archive.open(archive_file)) {
      std::cerr << "Failed to open archive: " << archive_file << '\n';
      return 1;
    }

    uint32_t crc = 0;

    for (const auto &piece : archive) {
      if (piece.crc32 != arc::crc32(piece.data)) {
        std::cerr << "CRC32 mismatch for file: " << piece.name << '\n';
        std::cerr << "Expected: " << std::hex << piece.crc32 << ", Actual: " << std::hex << arc::crc32(piece.data)
                  << '\n';
        return 1;
      }
      crc ^= piece.crc32;
    }

    if (crc == archive.get_header_crc32()) {
      std::cout << "Archive integrity check passed." << '\n';
    } else {
      std::cerr << "Archive integrity check failed." << '\n';
      std::cerr << "Expected CRC32: " << std::hex << archive.get_header_crc32() << ", Actual CRC32: " << std::hex << crc
                << '\n';
      return 1;
    }
  }

#ifndef NDEBUG
  else if (argv[1][0] == 'd') {

    std::string archive_file = argv[2];
    arc::Arc archive;

    if (!archive.open(archive_file)) {
      std::cerr << "Failed to open archive: " << archive_file << '\n';
      return 1;
    }

    archive.debug_print();
    // Close the archive
  }
#endif

  return 0;
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
