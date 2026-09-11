#include <arc.hpp>
#include <arc_fuse.hpp>
#include <fuse3/fuse_lowlevel.h>

using namespace arc;

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

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
