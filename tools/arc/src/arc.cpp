#include <arc.hpp>
#include <arc_fuse.hpp>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fuse3/fuse_lowlevel.h>

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

uint32_t crc32(const std::span<const std::byte> &data) {
  return crc32(reinterpret_cast<const uint8_t *>(data.data()), data.size());
}

const char *strdup(const char *str) {
  if (!str)
    return nullptr;
  size_t len = std::strlen(str);
  char *copy = new char[len + 1];
  std::memcpy(copy, str, len + 1);
  return copy;
}

Arc::Arc() : file_(nullptr) { header_.file_count = 0; };

Arc::Arc(const std::string &filename) : file_(nullptr) {
  header_.file_count = 0;
  this->open(filename);
};

Arc::~Arc() { this->close(); };

Arc::Arc(Arc &&other) noexcept : file_(other.file_), header_(other.header_) {
  other.file_ = nullptr;
  other.header_.file_count = 0;
};

Arc &Arc::operator=(Arc &&other) noexcept {
  if (this != &other) {
    if (file_) {
      std::fclose(file_);
    }
    if (!header_.pieces.empty()) {
      this->header_.pieces = std::move(other.header_.pieces);
    }
    file_ = other.file_;
    header_ = other.header_;
    other.file_ = nullptr;
    other.header_.file_count = 0;
  }
  return *this;
};

bool Arc::create(const std::string &filename) {
  filename_ = filename;
  file_ = std::fopen(filename.c_str(), "wb");
  if (!file_) {
    LOG_ERROR("Failed to create archive file: %s", filename.c_str());
    return false;
  }
  header_.version = VERSION;
  header_.file_count = 0;
  header_.crc32 = 0;
  LOG_DEBUG("Created archive file: %s", filename.c_str());
  return true;
};

bool Arc::parse() {
  if (!file_) {
    LOG_ERROR("Failed to open archive file: %s", filename_.c_str());
    return false;
  }
  // Read the header
  if (std::fread(&header_, HEADER_SIZE, 1, file_) != 1) {
    LOG_ERROR("Failed to read header from archive file: %s", filename_.c_str());
    std::fclose(file_);
    file_ = nullptr;
    return false;
  }
  // Validate the magic number
  if (std::memcmp(header_.magic, ARC_MAGIC, sizeof(ARC_MAGIC)) != 0) {
    LOG_ERROR("Invalid magic number in archive file: %s", filename_.c_str());
    std::fclose(file_);
    file_ = nullptr;
    return false;
  }
  // Validate the version
  if (header_.version != VERSION) {
    LOG_ERROR("Unsupported version in archive file: %s", filename_.c_str());
    std::fclose(file_);
    file_ = nullptr;
    return false;
  }
  // Read the pieces
  LOG_DEBUG("At %#lx reading in %u pieces", std::ftell(file_), header_.file_count);
  header_.pieces.reserve(header_.file_count);
  for (size_t i = 0; i < header_.file_count; ++i) {
    Piece piece;
    piece.name = nullptr;

    std::byte buffer[PIECE_SIZE];
    if (std::fread(&piece, PIECE_SIZE, 1, file_) != 1) {
      LOG_ERROR("Failed to read pieces from archive file: %s", filename_.c_str());
      std::fclose(file_);
      file_ = nullptr;
      return false;
    }
    header_.pieces.push_back(piece);
  }
  LOG_DEBUG("After pieace read at: %#lx", std::ftell(file_));
  // Populate the name pointers and data for each piece
  for (uint32_t i = 0; i < header_.file_count; ++i) {
    Piece &piece = header_.pieces[i];
    LOG_DEBUG("Validating piece %u: name_offset=%ld, name_size=%zu, offset=%ld, size=%zu", i, piece.name_offset,
              piece.name_size, piece.offset, piece.size);
    // Read the name
    LOG_DEBUG("Reading name for piece %u at offset %ld with size %zu", i, piece.name_offset, piece.name_size);
    if (std::fseek(file_, piece.name_offset, SEEK_SET)) {
      LOG_ERROR("Failed to seek to name offset for piece %u in archive file: %s", i, filename_.c_str());
      perror("fseek");
      std::fclose(file_);
      file_ = nullptr;
      return false;
    }
    char *name = new char[piece.name_size + 1];
    if (std::fread(name, 1, piece.name_size, file_) != piece.name_size) {
      LOG_ERROR("Failed to read name for piece %u from archive file: %s", i, filename_.c_str());
      delete[] name;
      std::fclose(file_);
      file_ = nullptr;
      return false;
    }
    name[piece.name_size] = '\0';
    piece.name = name;
    // Read the data
    std::fseek(file_, piece.offset, SEEK_SET);
    std::byte *data = new std::byte[piece.size];
    size_t read_size;
    LOG_DEBUG("Reading data for piece %u %zu byte", i, piece.size);
    if ((read_size = std::fread(data, 1, piece.size, file_)) != piece.size) {
      LOG_ERROR("Failed to read data for piece %u from archive file: %s", i, filename_.c_str());
      LOG_ERROR("read: %zu, expected: %zu", read_size, piece.size);
      perror("fread");
#ifndef NDEBUG
      debug_print();
#endif
      delete[] data;
      delete[] name;
      std::fclose(file_);
      file_ = nullptr;
      return false;
    }
    piece.data.insert(piece.data.end(), data, data + piece.size);
    delete[] data;
  }
  LOG_DEBUG("Opened archive file: %s with %u files", filename_.c_str(), header_.file_count);
  return true;
}

bool Arc::open(const std::string &filename) {
  filename_ = filename;
  file_ = std::fopen(filename.c_str(), "rb");
  return this->parse();
};

bool Arc::open(const unsigned char *data, size_t size) {
  filename_ = "<memory>";
  file_ = fmemopen((void *)data, size, "rb");
  return this->parse();
}

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
      .name = arc::strdup(sanitized_name.c_str()),
  };
  piece.data.reserve(size);

  std::byte *data = new std::byte[size];
  if (std::fread(data, 1, size, input_file) != size) {
    LOG_ERROR("Failed to read input file: %s", filename.c_str());
    std::fclose(input_file);
    return false;
  }
  piece.data.insert(piece.data.end(), data, data + size);
  delete[] data;
  std::fclose(input_file);

  piece.crc32 = crc32(piece.data);

  // Update the old pieces offset to account for the new piece

  for (uint32_t i = 0; i < header_.file_count; ++i) {
    header_.pieces[i].name_offset += PIECE_SIZE;
    header_.pieces[i].offset += PIECE_SIZE;
  }

  header_.pieces.push_back(piece);
  header_.file_count++;

  update_crc32();

  std::fseek(file_, 0, SEEK_SET);
  std::fwrite(&header_, HEADER_SIZE, 1, file_);
  for (uint32_t i = 0; i < header_.file_count; ++i) {
    std::fwrite(&header_.pieces[i], PIECE_SIZE, 1, file_);
  }
  for (uint32_t i = 0; i < header_.file_count; ++i) {
    std::fwrite(header_.pieces[i].name, 1, header_.pieces[i].name_size, file_);
    std::fwrite(header_.pieces[i].data.data(), 1, header_.pieces[i].size, file_);
  }

  modified_ = true;
  std::fflush(file_);
  return true;
};

bool Arc::add_file(const std::string &name_in_archive, const std::span<const std::byte> &data) {
  if (!file_) {
    LOG_ERROR("Archive file is not open");
    return false;
  }
  // Create a new piece

  // Remove any "../" from the name_in_archive to prevent directory traversal
  std::string sanitized_name = name_in_archive;
  size_t pos_;
  while ((pos_ = sanitized_name.find("../")) != std::string::npos) {
    sanitized_name.erase(pos_, 3);
  }
  long int pos = HEADER_SIZE + ((header_.file_count + 1) * PIECE_SIZE);
  for (uint32_t i = 0; i < header_.file_count; ++i) {
    pos += header_.pieces[i].name_size + header_.pieces[i].size;
  }
  LOG_DEBUG("Adding file: %s as %s to archive at %zx with size %zu", name_in_archive.c_str(), sanitized_name.c_str(),
            pos, data.size());
  Piece piece{
      .name_offset = pos,
      .name_size = sanitized_name.size(),
      .offset = static_cast<long>(pos + sanitized_name.size()),
      .size = data.size(),
      .crc32 = crc32(data),
      .name = arc::strdup(sanitized_name.c_str()),
  };
  piece.data.reserve(data.size());
  piece.data.insert(piece.data.end(), data.begin(), data.end());
  // Update the old pieces offset to account for the new piece
  header_.pieces.push_back(piece);

  for (uint32_t i = 0; i < header_.file_count; ++i) {
    header_.pieces[i].name_offset += PIECE_SIZE;
    header_.pieces[i].offset += PIECE_SIZE;
  }
  header_.file_count++;

  update_crc32();

  std::fseek(file_, 0, SEEK_SET);
  std::fwrite(&header_, HEADER_SIZE, 1, file_);
  for (uint32_t i = 0; i < header_.file_count; ++i) {
    std::fwrite(&header_.pieces[i], PIECE_SIZE, 1, file_);
  }
  for (uint32_t i = 0; i < header_.file_count; ++i) {
    std::fwrite(header_.pieces[i].name, 1, header_.pieces[i].name_size, file_);
    std::fwrite(header_.pieces[i].data.data(), 1, header_.pieces[i].size, file_);
  }
  std::fflush(file_);
  modified_ = true;
  return true;
}

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
  uint32_t i;
  for (i = 0; i < header_.file_count; ++i) {
    if (std::strcmp(header_.pieces[i].name, name_in_archive.c_str()) == 0) {
      break;
    }
  }

  // Found the piece, remove it
  delete[] header_.pieces[i].name;

  // Shift the remaining pieces down
  auto removed = std::find_if(header_.pieces.begin(), header_.pieces.end(),
                              [&](const Piece &p) { return std::strcmp(p.name, name_in_archive.c_str()) == 0; });

  if (removed != header_.pieces.end()) {
    header_.pieces.erase(removed);
  } else {
    LOG_ERROR("Failed to find piece to remove: %s", name_in_archive.c_str());
    return false;
  }

  header_.file_count--;
  update_crc32();
  // Rewrite the archive file without the removed piece
  std::fseek(file_, 0, SEEK_SET);
  std::fwrite(&header_, HEADER_SIZE, 1, file_);
  for (uint32_t j = 0; j < header_.file_count; ++j) {
    std::fwrite(&header_.pieces[j], PIECE_SIZE, 1, file_);
  }
  for (uint32_t j = 0; j < header_.file_count; ++j) {
    std::fwrite(header_.pieces[j].name, 1, header_.pieces[j].name_size, file_);
    std::fwrite(header_.pieces[j].data.data(), 1, header_.pieces[j].size, file_);
  }

  LOG_DEBUG("Removed file: %s from archive", name_in_archive.c_str());
  modified_ = true;
  return true;
}

bool Arc::mount_archive(const std::string &mount_point, int argc, char *argv[], bool daemonize, bool mt) {
  if (!file_) {
    LOG_ERROR("Archive file is not open");
    return false;
  }
  if (mounted_) {
    LOG_ERROR("Archive is already mounted");
    return false;
  }

  unsigned long total_size = HEADER_SIZE + header_.file_count * PIECE_SIZE;
  for (const auto &piece : *this) {
    total_size += piece.name_size + piece.size;
  }

  fuse::FileSystem fsys(this->filename_, total_size, [&](const fuse::Node &node) {
    if (node.is_dir())
      return;
    this->remove_file(node.name.substr(1));                                          // Remove leading '/' from name
    this->add_file(node.name.substr(1), std::get<fuse::FileData>(node.data).view()); // Add file back to archive
  });

  for (const auto &piece : *this) {
    fsys.insert_file(std::string("/") + piece.name, piece.data);
  }

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  args.allocated = 1; // Prevent fuse_opt_free_args from freeing argv
#if CONFIG_ARC_FUSE_IS_RO
  fuse_opt_add_arg(&args, "-oro");
#endif

  fuse_ = fuse_new(&args, &fuse::arc_fuse_ops, sizeof(fuse::arc_fuse_ops), &fsys);
  se_ = fuse_get_session(fuse_);
  if (!fuse_) {
    LOG_ERROR("Failed to create FUSE instance");
    return false;
  }
  if (fuse_mount(fuse_, mount_point.c_str()) != 0) {
    LOG_ERROR("Failed to mount FUSE filesystem at: %s", mount_point.c_str());
    fuse_opt_free_args(&args);
    fuse_destroy(fuse_);
    return false;
  }

  if (daemonize) {
    if (fuse_daemonize(1) != 0) {
      LOG_ERROR("Failed to daemonize FUSE filesystem");
      fuse_opt_free_args(&args);
      fuse_unmount(fuse_);
      fuse_destroy(fuse_);
      return false;
    }
  }

  int ret;
  LOG_DEBUG("Starting FUSE loop (multi-threaded: %s)", mt ? "true" : "false");
  mounted_ = true;
  if (mt) {
    if ((ret = fuse_session_loop_mt(se_, 1)) != 0) {
      LOG_ERROR("FUSE multi-threaded loop exited with error");
      fuse_opt_free_args(&args);
      fuse_unmount(fuse_);
      fuse_destroy(fuse_);
      return false;
    }
  } else {
    if ((ret = fuse_session_loop(se_)) != 0) {
      LOG_ERROR("FUSE loop exited with error");
      fuse_opt_free_args(&args);
      fuse_unmount(fuse_);
      fuse_destroy(fuse_);
      return false;
    }
  }

  LOG_DEBUG("FUSE loop exited with code: %d", ret);

  fuse_opt_free_args(&args);
  // fuse_unmount(fuse_);
  // fuse_destroy(fuse_);

  return true;
}

bool Arc::stop_mount() {
  if (!mounted_) {
    LOG_ERROR("Archive is not mounted");
    return false;
  }
  if (fuse_) {
    fuse_session_exit(se_);
    fuse_exit(fuse_);
    fuse_unmount(fuse_);
    fuse_destroy(fuse_);
    fuse_ = nullptr;
    se_ = nullptr;
    mounted_ = false;
    LOG_DEBUG("Unmounted archive");
  }
  return true;
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
  if (!header_.pieces.empty()) {
    for (auto &piece : header_.pieces) {
      delete[] piece.name;
      piece.data.clear();
    }
    header_.pieces.clear();
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
  if (log_level < DBG) {
    return;
  }
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
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
