#include "Presets/PresetData.h"

#include <stdexcept>

#include "json.hpp"

namespace ironclad
{
namespace
{
using nlohmann::json;

json refToJson(const std::optional<FileRef>& r)
{
  if (!r || r->empty())
    return nullptr;
  json j;
  j["path"] = r->path;
  if (!r->relative.empty())
    j["relative"] = r->relative;
  j["file"] = r->fileName;
  if (!r->sha256.empty())
    j["sha256"] = r->sha256;
  if (r->size)
    j["size"] = r->size;
  return j;
}

std::optional<FileRef> refFromJson(const json& j)
{
  if (!j.is_object())
    return std::nullopt;
  FileRef r;
  r.path = j.value("path", std::string{});
  r.relative = j.value("relative", std::string{});
  r.fileName = j.value("file", std::string{});
  r.sha256 = j.value("sha256", std::string{});
  r.size = j.value("size", std::uint64_t{0});
  if (r.empty())
    return std::nullopt;
  return r;
}
} // namespace

double PresetData::valueFor(int paramIndex) const
{
  const ParamSpec& spec = paramSpec(paramIndex);
  auto it = params.find(std::string(spec.id));
  return clampToRange(spec, it != params.end() ? it->second : spec.defaultValue);
}

void PresetData::setFromArray(const double* values, int count)
{
  for (int i = 0; i < count && i < kNumParams; ++i)
    params[std::string(paramSpec(i).id)] = values[i];
}

std::string presetToJson(const PresetData& p)
{
  json j;
  j["format"] = "IRONCLAD preset";
  j["schema"] = PresetData::kSchema;
  j["name"] = p.name;
  j["category"] = p.category;
  json params = json::object();
  for (const auto& [id, v] : p.params)
    params[id] = v;
  j["params"] = params;
  j["amp"] = {{"capture", refToJson(p.capture)}};
  j["cab"] = {{"irA", refToJson(p.ir[0])}, {"irB", refToJson(p.ir[1])}};
  if (p.templateOnly)
    j["template"] = true; // effect / level template: loading keeps the current capture and IRs
  return j.dump(2);
}

PresetData presetFromJson(const std::string& text)
{
  json j;
  try
  {
    j = json::parse(text);
  }
  catch (const std::exception&)
  {
    throw std::runtime_error("The preset file is damaged (invalid JSON).");
  }
  if (!j.is_object() || j.value("format", std::string{}) != "IRONCLAD preset")
    throw std::runtime_error("This is not an IRONCLAD preset.");
  const int schema = j.value("schema", 0);
  if (schema < 1)
    throw std::runtime_error("The preset file has no valid schema version.");
  if (schema > PresetData::kSchema)
    throw std::runtime_error("This preset was made with a newer version of IRONCLAD.");
  // Future schema migrations go here: if (schema == 1) { ... upgrade j to 2 ... }

  PresetData p;
  p.name = j.value("name", std::string{});
  p.category = j.value("category", std::string{});
  p.templateOnly = j.value("template", false);
  if (j.contains("params") && j["params"].is_object())
    for (auto it = j["params"].begin(); it != j["params"].end(); ++it)
      if (it.value().is_number())
        p.params[it.key()] = it.value().get<double>();
  if (j.contains("amp") && j["amp"].is_object())
    p.capture = refFromJson(j["amp"].value("capture", json()));
  if (j.contains("cab") && j["cab"].is_object())
  {
    p.ir[0] = refFromJson(j["cab"].value("irA", json()));
    p.ir[1] = refFromJson(j["cab"].value("irB", json()));
  }
  return p;
}

} // namespace ironclad
