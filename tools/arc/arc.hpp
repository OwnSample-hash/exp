#pragma once
#include <cstdint>
#include <string>

#define STR_STR(x) #x
#define STR(x) STR_STR(x)

#if defined(NDEBUG)
#define LOG_LOG(file, line, lvl, msg, ...) ((void)0)
#else
#define LOG_LOG(file, line, lvl, msg, ...) fprintf(stderr, "[%s:%d] " STR(lvl) " " msg "\n", file, line, ##__VA_ARGS__)
#endif

#define LOG_ERROR(msg, ...) LOG_LOG(__FILE__, __LINE__, ERR, msg, ##__VA_ARGS__)
#define LOG_INFO(msg, ...) LOG_LOG(__FILE__, __LINE__, INF, msg, ##__VA_ARGS__)
#define LOG_DEBUG(msg, ...) LOG_LOG(__FILE__, __LINE__, DBG, msg, ##__VA_ARGS__)

namespace arc {

#define ARC_MAGIC_M {0x01, 0x41, 0x52, 0x43}

const uint8_t ARC_MAGIC[] = ARC_MAGIC_M;

const uint32_t VERSION = 0x00000001;

uint32_t crc32(const uint8_t *data, size_t length);

#define SMARKER 0xDEADBEEFC0FEBABE
#define EMARKER 0xDEADBEEF

struct __attribute__((__packed__)) Piece {
  unsigned long smarker = SMARKER; //< Marker to identify the start of a piece
  long int name_offset;            //< Offset from the start of the file to the name string
  unsigned long name_size;         //< Size of the name string (not including null terminator)
  long int offset;                 //< Offset from the start of the file to the data
  unsigned long size;              //< Size of the data in bytes
  uint32_t crc32;                  //< CRC32 checksum of the data
  uint32_t emarker = EMARKER;      //< Marker to identify the end of a piece

  // excluded from serialization
  const char *name; //< Name of the file (null-terminated string)
  uint8_t *data;    //< Pointer to the data (not null-terminated)
};

constexpr size_t PIECE_SIZE = sizeof(Piece) - sizeof(const char *) - sizeof(uint8_t *);

struct __attribute__((__packed__)) Header {
  uint8_t magic[4] = ARC_MAGIC_M; //< Magic number to identify the archive format
  uint32_t version;               //< Version of the archive format
  uint32_t file_count;            //< Number of files in the archive
  uint32_t crc32;                 //< CRC32 checksum of the entire archive (excluding the magic number and version)

  // excluded from serialization
  Piece *pieces; //< Pointer to an array of Piece structures
};

constexpr size_t HEADER_SIZE = sizeof(Header) - sizeof(Piece *);

class Arc {
  std::FILE *file_;
  Header header_;
  bool modified_ = false;

  bool validate_piece(const Piece &piece) const;

  void update_crc32() {
    uint32_t crc = 0;
    for (uint32_t i = 0; i < header_.file_count; ++i) {
      crc ^= header_.pieces[i].crc32;
    }
    header_.crc32 = crc;
  }

public:
  Arc();
  Arc(const std::string &filename);
  ~Arc();

  Arc(const Arc &) = delete;
  Arc &operator=(const Arc &) = delete;

  Arc(Arc &&other) noexcept;
  Arc &operator=(Arc &&other) noexcept;

  bool create(const std::string &filename);
  bool open(const std::string &filename);
  bool add_file(const std::string &filename) { return add_file(filename, filename); };
  bool add_file(const std::string &filename, const std::string &name_in_archive);
  bool add_dir(const std::string &dirpath, const std::string &base_path = "");
  bool extract_file(const std::string &name_in_archive, const std::string &output_filename);
  bool remove_file(const std::string &name_in_archive);
  bool close();
  const Piece &get_piece(uint32_t index) const;
  const Piece &get_piece(const std::string &name) const;

  const Piece &operator[](uint32_t index) const { return get_piece(index); };
  const Piece &operator[](const std::string &name) const { return get_piece(name); };

  uint32_t get_file_count() const { return header_.file_count; };

#ifndef NDEBUG
  void debug_print() const;
#endif
};

} // namespace arc
// Vim: set expandtab tabstop=2 shiftwidth=2:
