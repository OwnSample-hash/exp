#include "arc.hpp"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <mode;c/x/l/v> <archive_file> <c:files_to_add/x:output_dir>" << std::endl;
    return 1;
  }

  if (argv[1][0] == 'c') {
    std::string archive_file = argv[2];
    arc::Arc archive;

    if (!archive.create(archive_file)) {
      std::cerr << "Failed to create archive: " << archive_file << std::endl;
      return 1;
    }

    for (int i = 3; i < argc; ++i) {
      LOG_DEBUG("Adding file/directory: %s", argv[i]);
      if (fs::is_directory(argv[i])) {
        if (!archive.add_dir(argv[i])) {
          std::cerr << "Failed to add directory: " << argv[i] << std::endl;
          return 1;
        }
      } else {
        if (!archive.add_file(argv[i])) {
          std::cerr << "Failed to add file: " << argv[i] << std::endl;
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
      std::cerr << "Failed to open archive: " << archive_file << std::endl;
      return 1;
    }

    // Example usage: list files in the archive
    for (uint32_t i = 0; i < archive.get_file_count(); ++i) {
      const arc::Piece &piece = archive.get_piece(i);
      std::cout << "File: " << piece.name << ", Size: " << piece.size << " bytes" << std::endl;
      std::cout << "Extracting file: " << piece.name << std::endl;
      fs::path output_path = fs::path(output_dir) / piece.name;
      if (!archive.extract_file(piece.name, output_path.string())) {
        std::cerr << "Failed to extract file: " << piece.name << std::endl;
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
      std::cerr << "Failed to open archive: " << archive_file << std::endl;
      return 1;
    }

    // Example usage: list files in the archive
    for (uint32_t i = 0; i < archive.get_file_count(); ++i) {
      const arc::Piece &piece = archive.get_piece(i);
      std::cout << "File: " << piece.name << ", Size: " << piece.size << " bytes" << std::endl;
    }
  } else if (argv[1][0] == 'v') {
    std::cout << argv[0] << " version " << arc::VERSION << std::endl;
  }

#ifndef NDEBUG
  else if (argv[1][0] == 'd') {

    std::string archive_file = argv[2];
    arc::Arc archive;

    if (!archive.open(archive_file)) {
      std::cerr << "Failed to open archive: " << archive_file << std::endl;
      return 1;
    }

    archive.debug_print();
    // Close the archive
  }
#endif

  return 0;
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
