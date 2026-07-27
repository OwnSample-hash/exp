#include "arc.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace arc {

uint32_t crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int j = 0; j < 8; ++j) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xEDB88320;
      } else {
        crc >>= 1;
      }
    }
  }
  return ~crc;
}

const char *strdup(const char *str) {
  if (!str)
    return nullptr;
  size_t len = std::strlen(str);
  char *copy = new char[len + 1];
  std::memcpy(copy, str, len + 1);
  return copy;
}

Arc::Arc() : file_(nullptr) {
  header_.pieces = nullptr;
  header_.file_count = 0;
};

Arc::Arc(const std::string &filename) : file_(nullptr) {
  header_.pieces = nullptr;
  header_.file_count = 0;
  open(filename);
};

Arc::~Arc() { this->close(); };

Arc::Arc(Arc &&other) noexcept : file_(other.file_), header_(other.header_) {
  other.file_ = nullptr;
  other.header_.pieces = nullptr;
  other.header_.file_count = 0;
};

Arc &Arc::operator=(Arc &&other) noexcept {
  if (this != &other) {
    if (file_) {
      std::fclose(file_);
    }
    if (header_.pieces) {
      delete[] header_.pieces;
    }
    file_ = other.file_;
    header_ = other.header_;
    other.file_ = nullptr;
    other.header_.pieces = nullptr;
    other.header_.file_count = 0;
  }
  return *this;
};

bool Arc::create(const std::string &filename) {
  file_ = std::fopen(filename.c_str(), "wb");
  if (!file_) {
    LOG_ERROR("Failed to create archive file: %s", filename.c_str());
    return false;
  }
  header_.version = VERSION;
  header_.file_count = 0;
  header_.pieces = nullptr;
  header_.crc32 = 0;
  LOG_DEBUG("Created archive file: %s", filename.c_str());
  return true;
};

bool Arc::open(const std::string &filename) {
  file_ = std::fopen(filename.c_str(), "rb");
  if (!file_) {
    LOG_ERROR("Failed to open archive file: %s", filename.c_str());
    return false;
  }
  // Read the header
  if (std::fread(&header_, HEADER_SIZE, 1, file_) != 1) {
    LOG_ERROR("Failed to read header from archive file: %s", filename.c_str());
    std::fclose(file_);
    file_ = nullptr;
    return false;
  }
  // Validate the magic number
  if (std::memcmp(header_.magic, ARC_MAGIC, sizeof(ARC_MAGIC)) != 0) {
    LOG_ERROR("Invalid magic number in archive file: %s", filename.c_str());
    std::fclose(file_);
    file_ = nullptr;
    return false;
  }
  // Validate the version
  if (header_.version != VERSION) {
    LOG_ERROR("Unsupported version in archive file: %s", filename.c_str());
    std::fclose(file_);
    file_ = nullptr;
    return false;
  }
  // Read the pieces
  LOG_DEBUG("At %#lx reading in %u pieces", std::ftell(file_), header_.file_count);
  header_.pieces = new Piece[header_.file_count];
  assert(header_.pieces != nullptr);
  for (size_t i = 0; i < header_.file_count; ++i) {
    std::memset(&header_.pieces[i], 0, sizeof(Piece));
    if (std::fread(header_.pieces + i, PIECE_SIZE, 1, file_) != 1) {
      LOG_ERROR("Failed to read pieces from archive file: %s", filename.c_str());
      delete[] header_.pieces;
      header_.pieces = nullptr;
      std::fclose(file_);
      file_ = nullptr;
      return false;
    }
  }
  LOG_DEBUG("After pieace read at: %#lx", std::ftell(file_));
  // Populate the name pointers and data for each piece
  for (uint32_t i = 0; i < header_.file_count; ++i) {
    Piece &piece = header_.pieces[i];
    LOG_DEBUG("Validating piece %u: smarker=%#lx, name_offset=%ld, name_size=%zu, offset=%ld, size=%zu, emarker=%#x", i,
              piece.smarker, piece.name_offset, piece.name_size, piece.offset, piece.size, piece.emarker);
    assert(piece.smarker == SMARKER);
    assert(piece.emarker == EMARKER);
    assert(piece.name_offset > 0);
    assert(piece.name_size > 0);
    assert(piece.offset > 0);
    assert(piece.size > 0);
    // Read the name
    LOG_DEBUG("Reading name for piece %u at offset %ld with size %zu", i, piece.name_offset, piece.name_size);
    if (std::fseek(file_, piece.name_offset, SEEK_SET)) {
      LOG_ERROR("Failed to seek to name offset for piece %u in archive file: %s", i, filename.c_str());
      perror("fseek");
      delete[] header_.pieces;
      header_.pieces = nullptr;
      std::fclose(file_);
      file_ = nullptr;
      return false;
    }
    char *name = new char[piece.name_size + 1];
    if (std::fread(name, 1, piece.name_size, file_) != piece.name_size) {
      LOG_ERROR("Failed to read name for piece %u from archive file: %s", i, filename.c_str());
      delete[] name;
      delete[] header_.pieces;
      header_.pieces = nullptr;
      std::fclose(file_);
      file_ = nullptr;
      return false;
    }
    name[piece.name_size] = '\0';
    piece.name = name;
    // Read the data
    std::fseek(file_, piece.offset, SEEK_SET);
    uint8_t *data = new uint8_t[piece.size];
    size_t read_size;
    LOG_DEBUG("Reading data for piece %u %zu byte", i, piece.size);
    if ((read_size = std::fread(data, 1, piece.size, file_)) != piece.size) {
      LOG_ERROR("Failed to read data for piece %u from archive file: %s", i, filename.c_str());
      LOG_ERROR("read: %zu, expected: %zu", read_size, piece.size);
      perror("fread");
#ifndef NDEBUG
      debug_print();
#endif
      delete[] data;
      delete[] name;
      delete[] header_.pieces;
      header_.pieces = nullptr;
      std::fclose(file_);
      file_ = nullptr;
      return false;
    }
    piece.data = data;
  }
  LOG_DEBUG("Opened archive file: %s with %u files", filename.c_str(), header_.file_count);
  return true;
};

bool Arc::add_file(const std::string &filename, const std::string &name_in_archive) {
  if (!file_) {
    LOG_ERROR("Archive file is not open");
    return false;
  }
  // Open the file to add
  FILE *input_file = std::fopen(filename.c_str(), "rb");
  if (!input_file) {
    LOG_ERROR("Failed to open input file: %s", filename.c_str());
    return false;
  }
  // Remove any "../" from the name_in_archive to prevent directory traversal
  std::string sanitized_name = name_in_archive;
  size_t pos_;
  while ((pos_ = sanitized_name.find("../")) != std::string::npos) {
    sanitized_name.erase(pos_, 3);
  }
  // Get the size of the input file
  std::fseek(input_file, 0, SEEK_END);
  size_t size = std::ftell(input_file);
  std::fseek(input_file, 0, SEEK_SET);
  // Allocate memory for the file data
  assert(size > 0);
  char *data = new char[size];
  if (std::fread(data, 1, size, input_file) != size) {
    LOG_ERROR("Failed to read input file: %s", filename.c_str());
    delete[] data;
    std::fclose(input_file);
    return false;
  }
  std::fclose(input_file);
  // Create a new piece

  long int pos = HEADER_SIZE + ((header_.file_count + 1) * PIECE_SIZE);
  for (uint32_t i = 0; i < header_.file_count; ++i) {
    pos += header_.pieces[i].name_size + header_.pieces[i].size;
  }
  LOG_DEBUG("Adding file: %s as %s to archive at %zx with size %zu", filename.c_str(), sanitized_name.c_str(), pos,
            size);
  Piece piece{
      .name_offset = pos,
      .name_size = sanitized_name.size(),
      .offset = static_cast<long>(pos + sanitized_name.size()),
      .size = size,
      .crc32 = crc32(reinterpret_cast<const uint8_t *>(data), size),
      .name = arc::strdup(sanitized_name.c_str()),
      .data = reinterpret_cast<uint8_t *>(data),
  };
  // Update the old pieces offset to account for the new piece

  for (uint32_t i = 0; i < header_.file_count; ++i) {
    header_.pieces[i].name_offset += PIECE_SIZE;
    header_.pieces[i].offset += PIECE_SIZE;
  }

  // #ifndef NDEBUG
  //   std::FILE *test_file = std::fopen((sanitized_name + ".piece").c_str(), "wb");
  //   if (!test_file) {
  //     LOG_ERROR("Failed to create test piece file");
  //     delete[] data;
  //     return false;
  //   }
  //   if (std::fwrite(&piece, PIECE_SIZE, 1, test_file) != 1) {
  //     LOG_ERROR("Failed to write test piece file");
  //   }
  //   std::fclose(test_file);
  // #endif

  // Rewrite the archive with the new piece count

  Piece *new_pieces = new Piece[header_.file_count + 1];
  if (header_.pieces) {
    std::memcpy(new_pieces, header_.pieces, sizeof(Piece) * header_.file_count);
    delete[] header_.pieces;
  }
  new_pieces[header_.file_count] = piece;
  header_.pieces = new_pieces;
  header_.file_count++;
  update_crc32();

  std::fseek(file_, 0, SEEK_SET);
  std::fwrite(&header_, HEADER_SIZE, 1, file_);
  for (uint32_t i = 0; i < header_.file_count; ++i) {
    std::fwrite(&header_.pieces[i], PIECE_SIZE, 1, file_);
  }
  for (uint32_t i = 0; i < header_.file_count; ++i) {
    std::fwrite(header_.pieces[i].name, 1, header_.pieces[i].name_size, file_);
    std::fwrite(header_.pieces[i].data, 1, header_.pieces[i].size, file_);
  }

  std::fseek(file_, 0, SEEK_END);

  // Write the name and data to the archive file
  if (std::fwrite(name_in_archive.c_str(), 1, name_in_archive.size(), file_) != name_in_archive.size()) {
    LOG_ERROR("Failed to write name to archive file: %s", name_in_archive.c_str());
    delete[] data;
    return false;
  }
  if (std::fwrite(data, 1, size, file_) != size) {
    LOG_ERROR("Failed to write data to archive file: %s", filename.c_str());
    delete[] data;
    return false;
  }

  modified_ = true;
  return true;
};

bool Arc::add_dir(const std::string &dirpath, const std::string &base_path) {
  if (!file_) {
    LOG_ERROR("Archive file is not open");
    return false;
  }
  for (const auto &entry : fs::recursive_directory_iterator(dirpath)) {
    if (entry.is_regular_file()) {
      std::string relative_path = fs::relative(entry.path(), dirpath).string();
      std::string name_in_archive = base_path.empty() ? relative_path : base_path + "/" + relative_path;
      if (!add_file(entry.path().string(), name_in_archive)) {
        LOG_ERROR("Failed to add file: %s to archive", entry.path().string().c_str());
        return false;
      }
    }
  }
  return true;
}

bool Arc::extract_file(const std::string &name_in_archive, const std::string &output_filename) {
  if (!file_) {
    LOG_ERROR("Archive file is not open");
    return false;
  }
  // Find the piece with the given name
  for (uint32_t i = 0; i < header_.file_count; ++i) {
    if (std::strcmp(header_.pieces[i].name, name_in_archive.c_str()) == 0) {
      // Found the piece, extract it
      fs::path output_path(output_filename);
      if (output_path.has_parent_path()) {
        fs::create_directories(output_path.parent_path());
      }
      std::fseek(file_, header_.pieces[i].offset, SEEK_SET);
      char *data = new char[header_.pieces[i].size];
      if (std::fread(data, 1, header_.pieces[i].size, file_) != header_.pieces[i].size) {
        LOG_ERROR("Failed to read data from archive file");
        delete[] data;
        return false;
      }
      // Write the data to the output file
      FILE *output_file = std::fopen(output_filename.c_str(), "wb");
      if (!output_file) {
        LOG_ERROR("Failed to open output file: %s", output_filename.c_str());
        delete[] data;
        return false;
      }
      if (std::fwrite(data, 1, header_.pieces[i].size, output_file) != header_.pieces[i].size) {
        LOG_ERROR("Failed to write data to output file: %s", output_filename.c_str());
        delete[] data;
        std::fclose(output_file);
        return false;
      }
      delete[] data;
      std::fclose(output_file);
      LOG_DEBUG("Extracted file: %s to %s", name_in_archive.c_str(), output_filename.c_str());
      return true;
    }
  }
  LOG_ERROR("File not found in archive: %s", name_in_archive.c_str());
  return false;
}

bool Arc::remove_file(const std::string &name_in_archive) {
  if (!file_) {
    LOG_ERROR("Archive file is not open");
    return false;
  }
  // Find the piece with the given name
  for (uint32_t i = 0; i < header_.file_count; ++i) {
    if (std::strcmp(header_.pieces[i].name, name_in_archive.c_str()) == 0) {
      // Found the piece, remove it
      Piece *new_pieces = new Piece[header_.file_count - 1];
      std::memcpy(new_pieces, header_.pieces, sizeof(Piece) * i);
      std::memcpy(new_pieces + i, header_.pieces + i + 1, sizeof(Piece) * (header_.file_count - i - 1));
      // Rewrite the archive file without the removed piece
      std::fclose(file_);
      file_ = std::fopen("temp.arc", "wb");
      if (!file_) {
        LOG_ERROR("Failed to create temporary archive file");
        delete[] new_pieces;
        return false;
      }
      // Write the header
      if (std::fwrite(&header_, HEADER_SIZE, 1, file_) != 1) {
        LOG_ERROR("Failed to write header to temporary archive file");
        delete[] new_pieces;
        std::fclose(file_);
        return false;
      }
      // Write the pieces
      if (std::fwrite(new_pieces, PIECE_SIZE, header_.file_count - 1, file_) != header_.file_count - 1) {
        LOG_ERROR("Failed to write pieces to temporary archive file");
        delete[] new_pieces;
        std::fclose(file_);
        return false;
      }
      delete[] header_.pieces;
      header_.pieces = new_pieces;
      header_.file_count--;
      update_crc32();
      LOG_DEBUG("Removed file: %s from archive", name_in_archive.c_str());
      return true;
    }
  }
  modified_ = true;
  LOG_ERROR("File not found in archive: %s", name_in_archive.c_str());
  return false;
}

bool Arc::close() {
  if (modified_) {
    // Update the header
    update_crc32();
    std::fseek(file_, 0, SEEK_SET);
    if (std::fwrite(&header_, HEADER_SIZE, 1, file_) != 1) {
      LOG_ERROR("Failed to write header to archive file");
      perror("fwrite");
      return false;
    }
  }
  if (header_.pieces) {
    for (uint32_t i = 0; i < header_.file_count; ++i) {
      delete[] header_.pieces[i].name;
      delete[] header_.pieces[i].data;
    }
    delete[] header_.pieces;
    header_.pieces = nullptr;
  }
  if (file_) {
    std::fclose(file_);
    file_ = nullptr;
  }
  modified_ = false;
  LOG_DEBUG("Closed archive file");
  return true;
}

const Piece &Arc::get_piece(uint32_t index) const {
  if (index >= header_.file_count) {
    throw std::out_of_range("Index out of range");
  }
  return header_.pieces[index];
}

const Piece &Arc::get_piece(const std::string &name) const {
  for (uint32_t i = 0; i < header_.file_count; ++i) {
    if (std::strcmp(header_.pieces[i].name, name.c_str()) == 0) {
      return header_.pieces[i];
    }
  }
  throw std::out_of_range("File not found in archive: " + name);
}

#ifndef NDEBUG
void Arc::debug_print() const {
  LOG_DEBUG("Archive debug print:");
  LOG_DEBUG("Version: %u", header_.version);
  unsigned int x = 1;
  unsigned char *c = (unsigned char *)&x;
  LOG_DEBUG("Endianness: %s", (*c) ? "Little Endian" : "Big Endian");

  LOG_DEBUG("Size of header ser: 0x%zx", HEADER_SIZE);
  LOG_DEBUG("Size of header rel: 0x%zx", sizeof(Header));
  LOG_DEBUG("Size of piece ser: 0x%zx", PIECE_SIZE);
  LOG_DEBUG("Size of piece rel: 0x%zx", sizeof(Piece));
  LOG_DEBUG("Size of long: %zu", sizeof(long));
  LOG_DEBUG("Size of uint32_t: %zu", sizeof(uint32_t));

  LOG_DEBUG("Magic: %c%c%c%c", header_.magic[0], header_.magic[1], header_.magic[2], header_.magic[3]);
  LOG_DEBUG("CRC32: %08X", header_.crc32);
  LOG_DEBUG("File count: %u", header_.file_count);
  for (uint32_t i = 0; i < header_.file_count; ++i) {
    const Piece &piece = header_.pieces[i];
    LOG_DEBUG("Piece %u:", i);
    LOG_DEBUG("  Name: %s", piece.name);
    LOG_DEBUG("  Offset: %ld (%#016lx)", piece.offset, piece.offset);
    LOG_DEBUG("  Size: %zu (%#016lx)", piece.size, piece.size);
    LOG_DEBUG("  CRC32: %08X", piece.crc32);
  }
}
#endif
} // namespace arc
// Vim: set expandtab tabstop=2 shiftwidth=2:
