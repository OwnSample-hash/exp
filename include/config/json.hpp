#pragma once
#include "config/type.hpp"
#include <interfaces/config_serializer.hpp>
#include <string_view>

namespace explo {

class JSONSerializer final : public IConfigSerializer {
public:
  JSONSerializer() = default;
  ~JSONSerializer() override = default;
  ConfigMap deserialize(const std::string_view file_path) override;
  void serialize(const ConfigMap &config, const std::string_view file_path) override;
  const char *getSuffix() const override { return ".json"; }
};

} // namespace explo
