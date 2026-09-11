#include <arc.hpp>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace arc;

bool Arc::mount_archive(const std::string &mount_point, int argc, char *argv[], bool daemonize, bool mt) {
  (void)argc;
  (void)argv;
  if (!file_) {
    LOG_ERROR("Archive file is not open");
    return false;
  }
  if (mounted_) {
    LOG_ERROR("Archive is already mounted");
    return false;
  }
  this->mount_point_ = fs::path(mount_point);
  if (!fs::exists(mount_point_)) {
    LOG_ERROR("Mount point does not exist: %s", mount_point.c_str());
    return false;
  }
  fs::create_directories(mount_point_.parent_path());
  for (const auto &piece : *this) {
    fs::path file_path = mount_point_ / piece.name;
    fs::create_directories(file_path.parent_path());
    std::ofstream ofs(file_path, std::ios::binary);
    if (!ofs) {
      LOG_ERROR("Failed to create file: %s", file_path.string().c_str());
      return false;
    }
    ofs.write(reinterpret_cast<const char *>(piece.data.data()), piece.size);
  }
  fs::permissions(mount_point_, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read,
                  fs::perm_options::replace);
  this->mounted_ = true;
  if (!mt && !daemonize) {
    while (mounted_) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
  return true;
};

bool Arc::stop_mount() {
  if (!mounted_) {
    LOG_ERROR("Archive is not mounted");
    return false;
  }
  fs::permissions(mount_point_, fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read,
                  fs::perm_options::replace);
  fs::remove_all(mount_point_);
  this->mounted_ = false;
  this->mount_point_.clear();
  return true;
};
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
