#pragma once
#include "../shared/sfq_shared.hpp"
#include <functional>
#include "GlobalNamespace/Saber.hpp"
#include "GlobalNamespace/SaberModelController.hpp"

namespace sfq {
// Reads config + saber list, (re)loads the selected AssetBundle if the selection changed.
// Returns true if a custom saber is ready to be attached.
bool prepareSelection();
// Replace the vanilla visuals of `controller` with the custom saber for `saber`'s hand.
void equip(GlobalNamespace::SaberModelController* controller, GlobalNamespace::Saber* saber);
// Called every frame for each Saber; pushes a pose and refreshes the ribbon mesh.
void tickTrail(GlobalNamespace::Saber* saber);
const Config& config();
std::vector<SaberEntry> listSabers();
// Applies a change to the config, clamps it, saves it. Takes effect when the next saber spawns.
void updateConfig(const std::function<void(Config&)>& edit);
}  // namespace sfq
