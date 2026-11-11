#include "doctest/doctest.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Parameters/Parameters.h"

using namespace ironclad;

#ifndef IRONCLAD_TEST_DATA_DIR
  #error "IRONCLAD_TEST_DATA_DIR must be defined by the build"
#endif

TEST_CASE("Parameter IDs are append-only relative to the committed snapshot")
{
  // Tests/param_ids.snapshot holds "<index> <id>" lines for every parameter ever
  // released. The current table must start with exactly those lines, in order.
  std::ifstream file(std::string(IRONCLAD_TEST_DATA_DIR) + "/param_ids.snapshot");
  REQUIRE_MESSAGE(file.good(), "param_ids.snapshot missing");

  std::vector<std::pair<int, std::string>> snapshot;
  std::string line;
  while (std::getline(file, line))
  {
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream ss(line);
    int index = -1;
    std::string id;
    ss >> index >> id;
    snapshot.emplace_back(index, id);
  }

  REQUIRE(snapshot.size() <= kParamSpecs.size());
  for (std::size_t i = 0; i < snapshot.size(); ++i)
  {
    CAPTURE(i);
    CHECK(snapshot[i].first == kParamSpecs[i].index);
    CHECK(snapshot[i].second == std::string(kParamSpecs[i].id));
  }
  // Reminder for the developer: new parameters must be added to the snapshot too.
  CHECK_MESSAGE(snapshot.size() == kParamSpecs.size(),
                "New parameters exist that are not in Tests/param_ids.snapshot — append them.");
}

TEST_CASE("Parameter lookup by stable id")
{
  CHECK(findParamIndex("global.input_db") == kParamInputGain);
  CHECK(findParamIndex("global.output_db") == kParamOutputGain);
  CHECK(findParamIndex("does.not.exist") == -1);
}

TEST_CASE("Parameter clamping")
{
  const auto& s = paramSpec(kParamInputGain);
  CHECK(clampToRange(s, -1000.0) == s.minValue);
  CHECK(clampToRange(s, 1000.0) == s.maxValue);
  CHECK(clampToRange(s, 3.0) == 3.0);
}
