#include <arc.hpp>
#include <arc_fuse.hpp>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <resolv.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#if CONFIG_ARC_FUSE_ENABLE_STAT_CHDEV
#include <sstream>
#endif

namespace arc {
namespace fuse {

void *arc_fuse_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
  (void)conn;
  (void)cfg;
  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  if (!fs) {
    LOG_ERROR("arc_fuse_init: fuse_get_context()->private_data is null");
  }
  fs->update_write_budget();

  return fs;
}

void arc_fuse_destroy(void *private_data) {
  (void)private_data;
  LOG_DEBUG("arc_fuse_destroy: cleaning up FUSE filesystem");
}

int arc_fuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  LOG_DEBUG("arc_fuse_read: path=%s, size=%zu, offset=%jd", path, size, static_cast<intmax_t>(offset));
  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  if (!fs) {
    LOG_ERROR("arc_fuse_read: fuse_get_context()->private_data is null");
    return -ENOENT;
  }

#if CONFIG_ARC_FUSE_ENABLE_STAT_CHDEV
  if (std::strcmp(path, "/stat") == 0) {
    LOG_DEBUG("arc_fuse_read: path is /stat, returning stat info");
    std::stringstream ss;
    auto &budget = fs->update_write_budget();

    ss << "In memory:\n"
       << "  Free space: " << budget.free_space << " bytes\n"
       << "  Used space: " << budget.used_space << " bytes\n"
       << "  Total space: " << budget.total_space << " bytes\n"
       << "  Free inodes: " << budget.node_free << "\n"
       << "  Node count: " << fs->file_count() << "\n"
       << "  Max bytes: " << CONFIG_ARC_FUSE_IN_MEMORY_MAX << " bytes\n";

    struct statvfs stbuf;
    if (statvfs(fs->archive_path().c_str(), &stbuf) != 0) {
      LOG_ERROR("arc_fuse_read: statvfs failed for archive path: %s", fs->archive_path().c_str());
      return -errno;
    }

    ss << "Archive:\n"
       << "  Path: " << fs->archive_path() << "\n"
       << "  Free space: " << stbuf.f_bavail * stbuf.f_frsize << " bytes\n"
       << "  Used space: " << (stbuf.f_blocks - stbuf.f_bfree) * stbuf.f_frsize << " bytes\n"
       << "  Total space: " << stbuf.f_blocks * stbuf.f_frsize << " bytes\n";

    LOG_DEBUG("arc_fuse_read: repsone is %zu bytes long returing %zu bytes", ss.str().size(), size);
    std::memcpy(buf, ss.str().c_str(), std::min(size, ss.str().size()));
    return static_cast<int>(std::min(size, ss.str().size()));
  }
#endif

  if (!fi && fi->fh == 0) {
    LOG_ERROR("arc_fuse_read: file handle is null for path: %s", path);
    return -EBADF;
  }
  auto *node = fs->get_node_from_fh(fi->fh);
  LOG_DEBUG("arc_fuse_read: reading from file: %s, size=%zu, offset=%jd", node->name.c_str(), size,
            static_cast<intmax_t>(offset));
  LOG_DEBUG("arc_fuse_read: file is dirty: %s", node->as_file().is_dirty() ? "true" : "false");
  if (!node->is_file())
    return -EISDIR;
  auto &file_data = node->as_file();
  auto file_size = file_data.size();
  if (offset >= static_cast<off_t>(file_size))
    return 0;
  size_t bytes_to_read = std::min(size, file_size - static_cast<size_t>(offset));
  LOG_DEBUG("arc_fuse_read: span size=%zu, bytes_to_read=%zu", file_data.view().size(), bytes_to_read);
  std::memcpy(buf, file_data.view().data() + offset, bytes_to_read);
  return static_cast<int>(bytes_to_read);
}

int arc_fuse_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
  LOG_DEBUG("arc_fuse_getattr: path=%s", path);

  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  if (!fs) {
    LOG_ERROR("arc_fuse_getattr: fuse_get_context()->private_data is null");
    return -ENOENT;
  }

#if CONFIG_ARC_FUSE_ENABLE_STAT_CHDEV
  if (std::strcmp(path, "/stat") == 0) {
    LOG_DEBUG("arc_fuse_getattr: path is /stat, returning character device");
    std::memset(stbuf, 0, sizeof(*stbuf));
    stbuf->st_mode = S_IFCHR | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_gid = fuse_get_context()->gid;
    stbuf->st_uid = fuse_get_context()->uid;
    stbuf->st_atime = fs->statbuf().st_atime;
    stbuf->st_ctime = fs->statbuf().st_ctime;
    stbuf->st_mtime = fs->statbuf().st_mtime;
    stbuf->st_rdev = makedev(0, 0); // Major and minor numbers can be set to 0 for a virtual device

    return 0;
  }
#endif

  Node *node = nullptr;
  if (!fi) {
    auto node_opt = fs->lookup(path);
    if (!node_opt)
      return -ENOENT;
    node = *node_opt;
  } else
    node = fs->get_node_from_fh(fi->fh);

  if (!node)
    return -ENOENT;

  LOG_DEBUG("arc_fuse_getattr: node name=%s, is_dir=%s, is_file=%s, is_dirty=%s", node->name.c_str(),
            node->is_dir() ? "true" : "false", node->is_file() ? "true" : "false",
            (node->is_file() ? (node->as_file().is_dirty() ? "true" : "false") : "N/A"));
  std::memset(stbuf, 0, sizeof(*stbuf));
  stbuf->st_gid = fuse_get_context()->gid;
  stbuf->st_uid = fuse_get_context()->uid;
  stbuf->st_atime = fs->statbuf().st_atime;
  stbuf->st_mtime = fs->statbuf().st_mtime;
  stbuf->st_ctime = fs->statbuf().st_ctime;

  if (node->is_dir()) {
#if CONFIG_ARC_FUSE_IS_RO
    stbuf->st_mode = S_IFDIR | 0444;
#else
    stbuf->st_mode = S_IFDIR | 0755;
#endif
    stbuf->st_nlink = 2;
    stbuf->st_size = 4096; // typical size for a directory
  } else {
#if CONFIG_ARC_FUSE_IS_RO
    stbuf->st_mode = S_IFREG | 0444;
#else
    stbuf->st_mode = S_IFREG | 0644;
#endif
    stbuf->st_nlink = 1;
    stbuf->st_size = static_cast<off_t>(node->as_file().size());
  }

  return 0;
}

int arc_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi,
                     enum fuse_readdir_flags flags) {
  LOG_DEBUG("arc_fuse_readdir: path=%s, offset=%jd", path, static_cast<intmax_t>(offset));
  (void)fi;
  (void)flags;
  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  auto node = fs->lookup(path);
  if (!node || !(*node)->is_dir())
    return -ENOENT;

  filler(buf, ".", nullptr, 0, {});
  filler(buf, "..", nullptr, 0, {});
  for (auto &[name, child] : (*node)->as_dir().children) {
    LOG_DEBUG("arc_fuse_readdir: adding entry: %s", name.c_str());
    filler(buf, name.c_str(), nullptr, 0, {});
  }
#if CONFIG_ARC_FUSE_ENABLE_STAT_CHDEV
  if (path == std::string("/")) {
    filler(buf, "stat", nullptr, 0, {});
  }
#endif
  return 0;
}

int arc_fuse_open(const char *path, struct fuse_file_info *fi) {
  LOG_DEBUG("arc_fuse_open: path=%s", path);
  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  auto node = fs->lookup(path);
  if (!node)
    return -ENOENT;
  if (!(*node)->is_file())
    return -EISDIR;
  fi->fh = fs->add_file_handle(*node);
  return 0;
}

int arc_fuse_statfs(const char *path, struct statvfs *stbuf) {
  LOG_DEBUG("arc_fuse_statfs: path=%s", path);
  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  std::memset(stbuf, 0, sizeof(*stbuf));
  constexpr unsigned long block_size = 4096;

  stbuf->f_bsize = block_size;
  stbuf->f_frsize = block_size;
  stbuf->f_namemax = 255;

  std::size_t node_count = fs->path_index().size();

  stbuf->f_files = node_count;
  auto &write_budget = fs->update_write_budget();

  if (fs->is_read_only()) {
    stbuf->f_bavail = 0;
    stbuf->f_bfree = 0;
    stbuf->f_ffree = 0;
    stbuf->f_blocks = write_budget.total_space / block_size;
  } else {
    stbuf->f_blocks = write_budget.total_space / block_size;
    stbuf->f_bavail = (write_budget.total_space - write_budget.used_space) / block_size;
    stbuf->f_bfree = write_budget.free_space / block_size;
    stbuf->f_ffree = write_budget.node_free;
  }
  return 0;
}

int arc_fuse_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
  LOG_DEBUG("arc_fuse_create: path=%s, mode=%o", path, mode);
  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  if (fs->is_read_only())
    return -EROFS;
  if (fs->lookup(path))
    return -EEXIST;
  if (mode & S_IFDIR)
    return -EISDIR;
  auto node = fs->create_file(path);
  fi->fh = fs->add_file_handle(node);
  return 0;
}

int arc_fuse_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  LOG_DEBUG("arc_fuse_write: path=%s, size=%zu, offset=%jd", path, size, static_cast<intmax_t>(offset));
  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  if (fs->is_read_only())
    return -EROFS;
  if (!fi && fi->fh == 0) {
    LOG_ERROR("arc_fuse_write: file handle is null for path: %s", path);
    return -EBADF;
  }
  auto node = fs->get_node_from_fh(fi->fh);
  if (!node->is_file())
    return -EISDIR;
  auto &file_data = node->as_file();
  auto &write_budget = fs->update_write_budget();
  if (write_budget.free_space < size) {
    LOG_ERROR("arc_fuse_write: not enough space to write %zu bytes to file: %s", size, path);
    return -ENOSPC;
  }

  auto cont = file_data.stage_for_write();

  if (offset + size > file_data.size())
    cont.resize(offset + size);
  // std::memcpy(cont.data() + offset, buf, size);
  cont.insert_range(cont.end(), std::span<const std::byte>(reinterpret_cast<const std::byte *>(buf), size));
  return size;
}

int arc_fuse_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
  LOG_DEBUG("arc_fuse_truncate: path=%s, size=%jd", path, static_cast<intmax_t>(size));
  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  if (fs->is_read_only())
    return -EROFS;
  if (fi->fh == 0)
    return -EBADF;
  auto node = reinterpret_cast<Node *>(&fi->fh);
  if (!node->is_file())
    return -EISDIR;
  auto &file_data = node->as_file();
  auto cont = file_data.stage_for_write();
  cont.resize(size);
  return 0;
}

int arc_fuse_unlink(const char *path) {
  LOG_DEBUG("arc_fuse_unlink: path=%s", path);
  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  if (fs->is_read_only())
    return -EROFS;
  auto result = fs->remove(path);
  if (!result)
    return -ENOENT;
  return 0;
}

int arc_fuse_mkdir(const char *path, mode_t mode) {
  LOG_DEBUG("arc_fuse_mkdir: path=%s, mode=%o", path, mode);
  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  if (fs->is_read_only())
    return -EROFS;
  if (fs->lookup(path))
    return -EEXIST;
  auto node = fs->create_dir(path);
  return 0;
}

int arc_fuse_rmdir(const char *path) {
  LOG_DEBUG("arc_fuse_rmdir: path=%s", path);
  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  if (fs->is_read_only())
    return -EROFS;
  auto result = fs->remove(path);
  if (!result)
    return -ENOENT;
  return 0;
}

int arc_fuse_rename(const char *from, const char *to, unsigned int flags) {
  LOG_DEBUG("arc_fuse_rename: from=%s, to=%s, flags=%u", from, to, flags);
  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  if (fs->is_read_only())
    return -EROFS;
  if (flags != 0)
    return -EINVAL;
  auto result = fs->rename(from, to);
  if (!result)
    return -ENOENT;
  return 0;
}

int arc_fuse_release(const char *path, struct fuse_file_info *fi) {
  LOG_DEBUG("arc_fuse_release: path=%s", path);
  if (!fi && fi->fh == 0)
    return -EBADF;
  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  auto node = reinterpret_cast<Node *>(&fi->fh);
  if (!node->is_file())
    return -EISDIR;
  fs->commit_write(node);
  fs->remove_file_handle(fi->fh);
  fi->fh = 0;
  return 0;
}

int arc_fuse_fsync(const char *path, int datasync, struct fuse_file_info *fi) {
  LOG_DEBUG("arc_fuse_fsync: path=%s, datasync=%d", path, datasync);
  (void)path;
  (void)datasync;
  if (!fi && fi->fh == 0)
    return -EBADF;
  auto *fs = static_cast<FileSystem *>(fuse_get_context()->private_data);
  auto node = reinterpret_cast<Node *>(&fi->fh);
  if (!node->is_file())
    return -EISDIR;
  fs->commit_write(node);
  return 0;
}

int arc_fuse_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
  LOG_DEBUG("arc_fuse_utimens: path=%s", path);
  (void)tv;
  (void)fi;
  // For now, we don't actually store timestamps in the archive, so just return success.
  return 0;
}

} // namespace fuse
} // namespace arc

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
