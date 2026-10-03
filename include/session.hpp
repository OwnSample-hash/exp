#pragma once

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <uuid.hpp>

using uuids::uuid;

namespace fs = std::filesystem;

namespace explo {

inline uuid NewUUID() {
  std::random_device rd;
  auto seed_data = std::array<int, std::mt19937::state_size>{};
  std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));
  std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
  std::mt19937 generator(seq);
  uuids::uuid_random_generator gen{generator};
  return gen();
}

struct Session;
using HeartBeat = std::function<bool(Session &)>;
using HeartBeatResolver = std::function<HeartBeat(const uuid &tool_id)>;

struct Session {
  uuid id;
  uuid toolId;
  bool isAlive{true};
  HeartBeat HeartBeat_;
  void *data{nullptr};
};

class SessionStore {
public:
  explicit SessionStore(fs::path path = "./sessions.yaml",
                        std::chrono::milliseconds heartbeat_interval = std::chrono::seconds(1));
  ~SessionStore();

  SessionStore(const SessionStore &) = delete;
  SessionStore(SessionStore &&) = delete;
  SessionStore &operator=(const SessionStore &) = delete;
  SessionStore &operator=(SessionStore &&) = delete;

  Session &addSession(uuid toolId, bool isAlive, HeartBeat heartbeat = {}, void *data = nullptr);
  Session &addSession(bool isAlive, HeartBeat heartbeat, void *data = nullptr);
  Session &at(const uuid &id);
  const Session &at(const uuid &id) const;
  Session &operator[](const uuid &id) { return at(id); }
  const Session &operator[](const uuid &id) const { return at(id); }
  bool removeSession(const uuid &id);
  std::size_t size() const;

  bool Save() const;
  bool Load(const HeartBeatResolver &heartbeat_resolver = {});

private:
  void runHeartbeats();

  std::unordered_map<uuid, Session> sessions_;
  mutable std::mutex mutex_;
  std::condition_variable heartbeat_condition_;
  std::thread heartbeat_thread_;
  bool stopping_{false};
  fs::path path_;
  std::chrono::milliseconds heartbeat_interval_;
};

} // namespace explo

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
