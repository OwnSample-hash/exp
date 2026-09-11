#pragma once
#include "config/type.hpp"
#include <interfaces/config_serializer.hpp>
#include <string_view>

namespace explo {

class YAMLSerializer final : public IConfigSerializer {
public:
  YAMLSerializer() = default;
  ~YAMLSerializer() override = default;
  ConfigMap deserialize(const std::string_view file_path) override;
  void serialize(const ConfigMap &config, const std::string_view file_path) override;
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
