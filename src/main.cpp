#include "log.hpp"
#include "saber_loader.hpp"
#include "ui.hpp"

#include "scotland2/shared/modloader.h"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"

#include "GlobalNamespace/Saber.hpp"
#include "GlobalNamespace/SaberModelController.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Color.hpp"

// SaberModelController::Init gained a Color parameter in newer game versions. If compiling
// against your bs-cordl fails on the next hook with "no matching ... Init", flip this to 0.
#ifndef SFQ_INIT_HAS_COLOR
#define SFQ_INIT_HAS_COLOR 1
#endif

#if SFQ_INIT_HAS_COLOR
MAKE_HOOK_MATCH(SaberModelController_Init, &GlobalNamespace::SaberModelController::Init, void,
                GlobalNamespace::SaberModelController* self, UnityEngine::Transform* parent,
                GlobalNamespace::Saber* saber, UnityEngine::Color color) {
    SaberModelController_Init(self, parent, saber, color);
#else
MAKE_HOOK_MATCH(SaberModelController_Init, &GlobalNamespace::SaberModelController::Init, void,
                GlobalNamespace::SaberModelController* self, UnityEngine::Transform* parent,
                GlobalNamespace::Saber* saber) {
    SaberModelController_Init(self, parent, saber);
#endif
    // Never let a mod failure take the game down: fall back to the vanilla saber.
    try {
        const bool ready = sfq::prepareSelection();
        SFQ_LOG("SaberModelController.Init: enabled=%d selected='%s' -> %s", sfq::config().enabled ? 1 : 0,
                sfq::config().selected.c_str(), ready ? "custom saber ready" : "NOT ready, vanilla saber stays");
        if (ready) sfq::equip(self, saber);
    } catch (const std::exception& e) {
        SFQ_ERR("equip failed: %s", e.what());
    } catch (...) {
        SFQ_ERR("equip failed: unknown exception");
    }
}

MAKE_HOOK_MATCH(Saber_ManualUpdate, &GlobalNamespace::Saber::ManualUpdate, void, GlobalNamespace::Saber* self) {
    Saber_ManualUpdate(self);
    try { sfq::tickTrail(self); } catch (...) {}
}

extern "C" __attribute__((visibility("default"))) void setup(CModInfo* info) noexcept {
    sfqLogInit();
    info->id = "saberfactory-quest";
    info->version = "0.1.0";
    info->version_long = 0;
    SFQ_LOG("setup");
}

extern "C" __attribute__((visibility("default"))) void late_load() noexcept {
    il2cpp_functions::Init();
    INSTALL_HOOK(getLogger(), SaberModelController_Init);
    INSTALL_HOOK(getLogger(), Saber_ManualUpdate);
    sfq::ui::registerMenu();
    SFQ_LOG("hooks installed");
}
