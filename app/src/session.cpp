#include <session.hpp>

#include <fstream>
#include <interfaces/tool.hpp>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

extern thread_local explo::ITool *currentTool;

namespace explo {

SessionStore::SessionStore(fs::path path, std::chrono::milliseconds heartbeat_interval)
    : path_(std::move(path)), heartbeat_interval_(heartbeat_interval) {
  if (heartbeat_interval_ <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("Heartbeat interval must be positive");
  }

  Load();
  heartbeat_thread_ = std::thread(&SessionStore::runHeartbeats, this);
}

SessionStore::~SessionStore() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  heartbeat_condition_.notify_one();
  if (heartbeat_thread_.joinable()) {
    heartbeat_thread_.join();
  }
  Save();
}

Session &SessionStore::addSession(uuid toolId, bool isAlive, HeartBeat heartbeat, void *data) {
  const uuid id = NewUUID();
  std::lock_guard<std::mutex> lock(mutex_);
  auto [iter, inserted] = sessions_.emplace(id, Session{id, toolId, isAlive, std::move(heartbeat), data});
  if (!inserted) {
    throw std::runtime_error("Session already exists");
  }
  return iter->second;
}

Session &SessionStore::addSession(bool isAlive, HeartBeat heartbeat, void *data) {
  return addSession(currentTool->getUUID(), isAlive, std::move(heartbeat), data);
}

Session &SessionStore::at(const uuid &id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.at(id);
}

const Session &SessionStore::at(const uuid &id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.at(id);
}

bool SessionStore::removeSession(const uuid &id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.erase(id) != 0;
}

std::size_t SessionStore::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.size();
}

bool SessionStore::Save() const {
  std::lock_guard<std::mutex> lock(mutex_);
  YAML::Emitter output;
  output << YAML::BeginSeq;
  for (const auto &[id, session] : sessions_) {
    output << YAML::BeginMap;
    output << YAML::Key << "id" << YAML::Value << uuids::to_string(session.id);
    output << YAML::Key << "toolId" << YAML::Value << uuids::to_string(session.toolId);
    output << YAML::Key << "isAlive" << YAML::Value << session.isAlive;
    output << YAML::EndMap;
  }
  output << YAML::EndSeq;

  std::ofstream file(path_);
  if (!file) {
    throw std::runtime_error("Failed to open session store file for writing: " + path_.string());
  }
  file << output.c_str();
  return file.good();
}

bool SessionStore::Load(const HeartBeatResolver &heartbeat_resolver) {
  std::ifstream file(path_);
  if (!file) {
    if (!fs::exists(path_)) {
      return false;
    }
    throw std::runtime_error("Failed to open session store file: " + path_.string());
  }

  YAML::Node root = YAML::Load(file);
  if (!root.IsSequence()) {
    throw std::runtime_error("Session store must contain a YAML sequence: " + path_.string());
  }

  std::unordered_map<uuid, Session> loaded;
  for (const auto &node : root) {
    const auto id = uuid::from_string(node["id"].as<std::string>());
    const auto toolId = uuid::from_string(node["toolId"].as<std::string>());
    if (!id || !toolId) {
      throw std::runtime_error("Invalid UUID in session store: " + path_.string());
    }
    HeartBeat heartbeat;
    if (heartbeat_resolver) {
      heartbeat = heartbeat_resolver(*toolId);
    }
    loaded.emplace(*id, Session{*id, *toolId, node["isAlive"].as<bool>(), std::move(heartbeat), nullptr});
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_ = std::move(loaded);
  }
  return true;
}

void SessionStore::runHeartbeats() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (!stopping_) {
    if (heartbeat_condition_.wait_for(lock, heartbeat_interval_, [this] { return stopping_; })) {
      break;
    }

    for (auto &[id, session] : sessions_) {
      if (session.isAlive && session.HeartBeat_ && !session.HeartBeat_(session)) {
        session.isAlive = false;
      }
    }
  }
}

} // namespace explo

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120: