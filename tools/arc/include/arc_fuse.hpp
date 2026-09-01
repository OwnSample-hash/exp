#pragma once

#include "arc_config.hpp"

#include <arc.hpp>
#include <config.hpp>
#include <expected>
#include <filesystem>
#include <functional>
#include <fuse3/fuse.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace arc {
namespace fuse {

class Node;
using NodePtr = std::unique_ptr<Node>;

struct ArchiveBacked {
  std::span<const std::byte> data;
};

struct Staged {
  std::vector<std::byte> data;
};

struct FileData {
  // Read-only files start ArchiveBacked; first write promotes to Staged.
  std::variant<ArchiveBacked, Staged> content;

  [[nodiscard]] size_t size() const {
    return std::visit([](auto &&c) { return c.data.size(); }, content);
  }

  [[nodiscard]] std::span<const std::byte> view() const {
    return std::visit([](auto &&c) -> std::span<const std::byte> { return c.data; }, content);
  }

  // Promote to a mutable staged buffer (copy-on-write), returns ref for writing.
  std::vector<std::byte> &stage_for_write() {
    if (auto *archived = std::get_if<ArchiveBacked>(&content)) {
      std::vector<std::byte> copy(archived->data.begin(), archived->data.end());
      content = Staged{std::move(copy)};
    }
    return std::get<Staged>(content).data;
  }

  [[nodiscard]] bool is_dirty() const { return std::holds_alternative<Staged>(content); }
};

struct DirData {
  // Owns children by name; enables both O(1) lookup-by-name AND stable iteration for readdir.
  std::unordered_map<std::string, NodePtr> children;
};

class Node {
public:
  std::string name;       // this component only, e.g. "readme.txt"
  Node *parent = nullptr; // non-owning; tree owns children, not parents
  std::variant<FileData, DirData> data;

  [[nodiscard]] bool is_dir() const { return std::holds_alternative<DirData>(data); }
  [[nodiscard]] bool is_file() const { return std::holds_alternative<FileData>(data); }

  [[nodiscard]] DirData &as_dir() { return std::get<DirData>(data); }
  [[nodiscard]] FileData &as_file() { return std::get<FileData>(data); }
};

enum class LookupError : std::uint8_t { NotFound, NotADirectory, NotAFile };

struct WriteBudget {
  uint64_t free_space;
  uint64_t used_space;
  uint64_t total_space;
  uint64_t node_free;
};

using StagedCallback = std::function<void(const Node &)>;

class FileSystem {
  void prune_path_index(std::string_view full_path, Node *node) {
    path_index_.erase(std::string(full_path));
    if (node->is_dir()) {
      for (auto &[child_name, child] : node->as_dir().children)
        prune_path_index(std::string(full_path) + "/" + child_name, child.get());
    }
  }

  NodePtr root_;
  std::size_t nodes_ = 0;
  fs::path archive_path_;
  struct stat statbuf_;
  std::size_t archive_size_ = 0;
  std::optional<StagedCallback> staged_callback_;
  std::unordered_map<std::string, Node *> path_index_; // non-owning; root_/children own the tree
  bool read_only_ = true; // if false, writes are staged in memory and can be flushed to the archive
  WriteBudget write_budget_{.free_space = 0, .used_space = 0, .total_space = 0, .node_free = 0};
  std::unordered_map<uint64_t, Node *> file_handle_map_; // maps fuse_file_info::fh to Node*

public:
  explicit FileSystem(fs::path &path, std::size_t size, std::optional<StagedCallback> staged_callback = std::nullopt)
      : archive_path_(std::move(path)), archive_size_(size), staged_callback_(std::move(staged_callback)) {
    root_ = std::make_unique<Node>();
    root_->name = "/";
    root_->data = DirData{};
    path_index_["/"] = root_.get();
    if (staged_callback_)
      read_only_ = false;
    if (stat(archive_path_.c_str(), &statbuf_) == 0) {
      write_budget_.total_space = statbuf_.st_size;
      write_budget_.used_space = archive_size_;
      write_budget_.free_space = write_budget_.total_space - write_budget_.used_space;
    }
  }

  [[nodiscard]] std::expected<Node *, LookupError> lookup(std::string_view path) const {
    auto it = path_index_.find(std::string(path));
    if (it == path_index_.end())
      return std::unexpected(LookupError::NotFound);
    return it->second;
  }

  // Splits "/docs/notes/todo.txt" -> creates synthetic dir nodes for
  // "/docs" and "/docs/notes" if they don't already exist, returns the
  // immediate parent directory node.
  Node *ensure_parent_dirs(std::string_view full_path) {
    Node *current = root_.get();
    std::string built_path;

    for (const auto &component : std::filesystem::path(full_path).parent_path()) {
      if (component == "/")
        continue;
      built_path += "/" + component.string();

      if (auto existing = lookup(built_path)) {
        current = *existing;
        continue;
      }
      auto new_dir = std::make_unique<Node>();
      new_dir->name = component.string();
      new_dir->parent = current;
      new_dir->data = DirData{};

      Node *raw = new_dir.get();
      current->as_dir().children.emplace(new_dir->name, std::move(new_dir));
      path_index_[built_path] = raw;
      current = raw;
    }
    return current;
  }

  // Insert a file discovered while parsing the archive.
  Node *insert_file(std::string_view full_path, std::span<const std::byte> file_bytes) {
    Node *parent = ensure_parent_dirs(full_path);
    auto name = std::filesystem::path(full_path).filename().string();

    auto node = std::make_unique<Node>();
    node->name = name;
    node->parent = parent;
    node->data = FileData{.content = ArchiveBacked{file_bytes}};

    Node *raw = node.get();
    parent->as_dir().children.emplace(name, std::move(node));
    path_index_[std::string(full_path)] = raw;
    nodes_++;
    return raw;
  }

  Node *create_file(std::string_view full_path) {
    Node *parent = ensure_parent_dirs(full_path);
    auto name = std::filesystem::path(full_path).filename().string();

    auto node = std::make_unique<Node>();
    node->name = name;
    node->parent = parent;
    node->data = FileData{.content = Staged{std::vector<std::byte>{}}};

    Node *raw = node.get();
    parent->as_dir().children.emplace(name, std::move(node));
    path_index_[std::string(full_path)] = raw;
    nodes_++;
    return raw;
  }

  Node *create_dir(std::string_view full_path) {
    Node *parent = ensure_parent_dirs(full_path);
    auto name = std::filesystem::path(full_path).filename().string();

    auto node = std::make_unique<Node>();
    node->name = name;
    node->parent = parent;
    node->data = DirData{};

    Node *raw = node.get();
    parent->as_dir().children.emplace(name, std::move(node));
    path_index_[std::string(full_path)] = raw;
    nodes_++;
    return raw;
  }

  // Remove a node: detach from parent's children (releases ownership/frees it)
  // and drop it (and, for directories, all descendants) from the path index.
  std::expected<void, LookupError> remove(std::string_view full_path) {
    auto found = lookup(full_path);
    if (!found)
      return std::unexpected(found.error());
    Node *node = *found;

    if (node->is_dir() && !node->as_dir().children.empty())
      return std::unexpected(LookupError::NotADirectory); // reuse or add DirNotEmpty

    prune_path_index(full_path, node);
    node->parent->as_dir().children.erase(node->name);
    file_handle_map_.erase(reinterpret_cast<uint64_t>(node));
    nodes_--;
    return {};
  }

  bool rename(std::string_view from, std::string_view to) {
    auto found = lookup(from);
    if (!found)
      return false;
    Node *node = *found;

    auto new_parent = ensure_parent_dirs(to);
    auto new_name = std::filesystem::path(to).filename().string();

    // Remove from old parent
    node->parent->as_dir().children.erase(node->name);

    // Update node's name and parent
    node->name = new_name;
    node->parent = new_parent;

    // Add to new parent
    new_parent->as_dir().children.emplace(new_name, std::unique_ptr<Node>(node));

    // Update path index
    prune_path_index(from, node);
    path_index_[std::string(to)] = node;

    return true;
  }

  bool commit_write(std::string_view full_path) {
    auto found = lookup(full_path);
    if (!found)
      return false;
    Node *node = *found;

    if (!node->is_file())
      return false;

    auto &file_data = node->as_file();
    if (!file_data.is_dirty())
      return true; // nothing to commit

    if (staged_callback_) {
      (*staged_callback_)(*node);
      return true;
    }
    return false;
  }

  bool commit_write(Node *node) {
    if (!node->is_file())
      return false;

    auto &file_data = node->as_file();
    if (!file_data.is_dirty())
      return true; // nothing to commit

    // std::string full_path;
    // for (Node *current = node; current != nullptr; current = current->parent) {
    //   if (current->parent != nullptr) { // skip root
    //     full_path = "/" + current->name + full_path;
    //   }
    // }

    if (staged_callback_) {
      (*staged_callback_)(*node);
      return true;
    }
    return false;
  }

  uint64_t add_file_handle(Node *node) {
    uint64_t fh = reinterpret_cast<uint64_t>(node);
    file_handle_map_[fh] = node;
    return fh;
  }

  void remove_file_handle(uint64_t fh) { file_handle_map_.erase(fh); }

  const WriteBudget &update_write_budget() {
    write_budget_.used_space = 0;
    for (const auto &[path, node] : path_index_)
      if (node->is_file() && node->as_file().is_dirty()) {
        LOG_DEBUG("Updating write budget: file %s is dirty, size=%zu", path.c_str(), node->as_file().size());
        write_budget_.used_space += node->as_file().size();
      }
    if (read_only_) {
      LOG_DEBUG("Filesystem is read-only");
      write_budget_.total_space = write_budget_.used_space;

      write_budget_.free_space = 0;
      write_budget_.node_free = 0;
      return this->write_budget_;
    }
    write_budget_.total_space = CONFIG_ARC_FUSE_IN_MEMORY_MAX;
    write_budget_.node_free = CONFIG_ARC_FUSE_FREE_INODES;
    write_budget_.free_space = write_budget_.total_space - write_budget_.used_space;

    LOG_DEBUG("Write budget updated: free_space=%zu, used_space=%zu, total_space=%zu, node_free=%zu",
              write_budget_.free_space, write_budget_.used_space, write_budget_.total_space, write_budget_.node_free);
    return this->write_budget_;
  }

  [[nodiscard]] Node *root() const { return root_.get(); }
  [[nodiscard]] bool is_read_only() const { return read_only_; }
  [[nodiscard]] std::size_t file_count() const { return nodes_; }
  [[nodiscard]] const fs::path &archive_path() const { return archive_path_; }
  [[nodiscard]] std::size_t archive_size() const { return archive_size_; }
  [[nodiscard]] const std::unordered_map<std::string, Node *> &path_index() const { return path_index_; }
  [[nodiscard]] const WriteBudget &write_budget() const { return write_budget_; }
  [[nodiscard]] Node *get_node_from_fh(uint64_t fh) const {
    auto it = file_handle_map_.find(fh);
    if (it != file_handle_map_.end())
      return it->second;
    return nullptr;
  }
  [[nodiscard]] const struct stat &statbuf() const { return statbuf_; }
};

void *arc_fuse_init(struct fuse_conn_info *conn, struct fuse_config *cfg);
void arc_fuse_destroy(void *private_data);

int arc_fuse_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
int arc_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi,
                     enum fuse_readdir_flags flags);
int arc_fuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int arc_fuse_open(const char *path, struct fuse_file_info *fi);
int arc_fuse_statfs(const char *path, struct statvfs *stbuf);

int arc_fuse_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int arc_fuse_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int arc_fuse_truncate(const char *path, off_t size, struct fuse_file_info *fi);
int arc_fuse_unlink(const char *path);
int arc_fuse_mkdir(const char *path, mode_t mode);
int arc_fuse_rmdir(const char *path);
int arc_fuse_rename(const char *from, const char *to, unsigned int flags);
int arc_fuse_release(const char *path, struct fuse_file_info *fi);
int arc_fuse_fsync(const char *path, int datasync, struct fuse_file_info *fi);
int arc_fuse_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi);

const struct fuse_operations arc_fuse_ops{
    .getattr = arc_fuse_getattr,
    .mkdir = arc_fuse_mkdir,
    .unlink = arc_fuse_unlink,
    .rmdir = arc_fuse_rmdir,
    .rename = arc_fuse_rename,
    .truncate = arc_fuse_truncate,
    .open = arc_fuse_open,
    .read = arc_fuse_read,
    .write = arc_fuse_write,
    .statfs = arc_fuse_statfs,
    .release = arc_fuse_release,
    .fsync = arc_fuse_fsync,
    .readdir = arc_fuse_readdir,
    .init = arc_fuse_init,
    .destroy = arc_fuse_destroy,
    .create = arc_fuse_create,
    .utimens = arc_fuse_utimens,
};

} // namespace fuse
} // namespace arc
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
