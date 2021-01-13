#include "ShaneLoads.hpp"
#include "Features/Session.hpp"
#include "Modules/Console.hpp"

Variable fps_max;

Variable sar_shane_loads("sar_shane_loads", "0", 0, 1, "Temporarily sets fps_max to 0 during loads");

ShaneLoads* shaneLoads;

ShaneLoads::ShaneLoads(void) {
  fps_max = Variable("fps_max");
  this->hasLoaded = true;
}

void ShaneLoads::Update(void) {
  if (!sar_shane_loads.GetBool()) return;

  bool isLoading = session->signonState == SIGNONSTATE_CHANGELEVEL;
  if (session->signonState == SIGNONSTATE_CHANGELEVEL) {
    this->inLoad = true;
    this->oldMax = fps_max.GetInt();
    fps_max.SetValue(0);
  } else if (this->inLoad && session->signonState == SIGNONSTATE_SPAWN) {
    fps_max.SetValue(this->oldMax);
  }
}
