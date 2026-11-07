/**
 * @file nc.hpp
 * @brief Header file for the nc module.
 *
 * Connections ("channels") live inside the tool object. At any moment at most one channel is in the
 * foreground (attached to the terminal); all others are in the background, where a dedicated reader thread
 * drains their sockets into per-channel buffers. The reader thread runs for the whole lifetime of the tool
 * (initialize -> shutdown), independently of invoke()/suppress().
 */
#pragma once

#include <arpa/inet.h>
#include <atomic>
#include <cstdint>
#include <cmd/var_snapshot.hpp>
#include <interfaces/tool.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <spdlog/logger.h>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace explo {
namespace builtin {

struct NCConfig {
  std::string host = "127.0.0.1";
  uint16_t port = htons(80);
  bool isTcp = true;
};

class NC final : public ITool {
  enum class ChannelState { Background, Foreground };

  struct Channel {
    int id = 0;
    int fd = -1;
    std::string host;
    uint16_t port = 0; // host byte order
    bool tcp = true;
    ChannelState state = ChannelState::Background;
    std::string buffer; // data received while in background, not yet shown
    bool closed = false; // remote closed / socket error (buffer may still hold unread data)
    std::string error;   // reason when closed by error
  };

  struct Listener {
    int id = 0;
    int fd = -1;
    uint16_t port = 0; // host byte order (actual port, even if 0 was requested)
  };

  /// Upper bound of unread data kept per channel. When reached the reader stops draining the socket, so TCP
  /// back-pressure applies instead of unbounded memory growth.
  static constexpr std::size_t kMaxBuffered = 1u << 20;

  std::shared_ptr<spdlog::logger> logger;
  NCConfig config;
  std::string prefix; // variable prefix, set by invoke()
  cmd::VarSnapshot varSnap; // preserves published variables across suppress()/invoke()

  // Background key, stored as caret notation (e.g. "^T"); bgKeyByte is the decoded control byte.
  std::string bgKeyNotation = "^T";
  unsigned char bgKeyByte = 0x14;

  // Channel table; everything below is protected by `mtx`.
  std::mutex mtx;
  std::map<int, Channel> channels;
  int nextId = 1;
  std::map<int, Listener> listeners; // accepted connections are turned into background channels
  int nextListenerId = 1;

  // Background reader thread + self-pipe used to wake it when the set of watched fds changes.
  std::thread reader;
  std::atomic<bool> running{false};
  int wakePipe[2] = {-1, -1};

  // --- helpers ---
  static bool send_all(int fd, const char *data, std::size_t len);
  static void write_all(int fd, const char *data, std::size_t len);
  static void set_nonblocking(int fd);
  static bool parseCaret(std::string_view notation, unsigned char &out);
  static bool describePeer(int fd, std::string &host, uint16_t &port);

  void setBackgroundKey(std::string_view notation);

  int addChannelLocked(int fd, std::string host, uint16_t port, bool tcp); // caller holds `mtx`
  int adoptLocked(int fd);                                                  // caller holds `mtx`; -1 if fd unusable
  int openChannel(const NCConfig &cfg);
  std::string startListener(uint16_t port);
  std::string stopListener(int id);
  std::string runForeground(int id);
  std::string listChannels();
  std::string closeChannel(int id);

  void backgroundLoop();
  void wakeReader();

public:
  NC() {};
  ~NC() override { shutdown(); }

  /**
   * Turns an already connected socket into a new background channel and returns the channel id.
   * Takes ownership of `fd` on success (it is set non-blocking and closed by the tool). On failure
   * (not a socket / invalid fd) it throws and the caller keeps ownership. Thread-safe.
   */
  int adoptConnection(int fd);

  const char *getName() const override { return "nc"; }
  const SemVer getVersion() const override { return {0, 0, 2}; }

  void initialize(initArgs &) override;
  void shutdown() override;

  void execute() override;

  void invoke(std::string_view prefix, bool soft = false) override;

  void suppress() override;

  ModuleType getModuleType() const override { return ModuleType::TOOL; }

  const std::vector<std::string> &getTags() const override {
    static std::vector<std::string> tags = {"network", "utility"};
    return tags;
  }

  const uuids::uuid getUUID() override { return uuids::uuid::from_string(NC_UUID).value(); }
};

} // namespace builtin
} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
