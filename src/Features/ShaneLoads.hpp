#pragma once

#include "Variable.hpp"
#include "Features/Feature.hpp"

class ShaneLoads : public Feature {
public:
  ShaneLoads(void);
  void Update(void);
private:
  bool inLoad = false;
  int oldMax = 0;
};

extern ShaneLoads* shaneLoads;

extern Variable sar_shane_loads;
