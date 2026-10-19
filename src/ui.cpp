#include "ui.hpp"
#include "saber_loader.hpp"
#include "log.hpp"

#include <string>
#include <vector>

#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Transform.hpp"

namespace sfq::ui {

// Strings must outlive the dropdown, so keep them static.
static std::vector<std::string>      g_names;
static std::vector<std::string_view> g_views;

static const char* kAccent = "#2EB6FF";   // SaberFactory-style blue accent

static void header(UnityEngine::Transform* t, const char* text) {
    std::string s = std::string("<size=125%><b><color=") + kAccent + ">" + text + "</color></b></size>";
    BSML::Lite::CreateText(t, s);
}

static void build(UnityEngine::GameObject* self) {
    auto* t = BSML::Lite::CreateScrollableSettingsContainer(self->get_transform())->get_transform();
    sfq::prepareSelection();                 // loads/creates config so values below are current
    const Config cfg = sfq::config();

    BSML::Lite::CreateText(t, std::string("<size=160%><b><color=") + kAccent + ">SaberFactory</color></b></size>");
    BSML::Lite::CreateText(t, "<size=80%>Changes apply the next time a song starts.</size>");

    // ---- Saber selection ----
    header(t, "Saber");
    auto sabers = sfq::listSabers();
    g_names.clear(); g_views.clear();
    g_names.push_back("Vanilla");
    for (auto& s : sabers) g_names.push_back(s.name);
    for (auto& n : g_names) g_views.push_back(n);
    int cur = 0;
    for (size_t i = 0; i < g_names.size(); ++i) if (g_names[i] == cfg.selected) cur = (int)i;

    BSML::Lite::CreateToggle(t, "Custom sabers", cfg.enabled,
        [](bool v) { sfq::updateConfig([&](Config& c) { c.enabled = v; }); });
    BSML::Lite::CreateDropdown(t, "Selected saber", g_names[cur], g_views,
        [](StringW v) {
            std::string name = static_cast<std::string>(v);
            sfq::updateConfig([&](Config& c) { c.selected = (name == "Vanilla") ? "" : name; });
        });
    if (sabers.empty())
        BSML::Lite::CreateText(t, "<size=80%>No sabers found. Put Android-built .saber files in\n"
                                  "ModData/com.beatgames.beatsaber/Mods/SaberFactoryQuest/Sabers</size>");

    // ---- Trail ----
    header(t, "Trail");
    BSML::Lite::CreateToggle(t, "Enable trail", cfg.trailEnabled,
        [](bool v) { sfq::updateConfig([&](Config& c) { c.trailEnabled = v; }); });
    BSML::Lite::CreateSliderSetting(t, "Trail width", 0.05f, cfg.trailWidth, 0.05f, 1.0f,
        [](float v) { sfq::updateConfig([&](Config& c) { c.trailWidth = v; }); });
    BSML::Lite::CreateSliderSetting(t, "Trail duration (s)", 0.05f, cfg.trailDuration, 0.05f, 2.0f,
        [](float v) { sfq::updateConfig([&](Config& c) { c.trailDuration = v; }); });
}

void registerMenu() {
    BSML::Init();
    BSML::Register::RegisterSettingsMenu("SaberFactory",
        [](UnityEngine::GameObject* self, bool firstActivation) {
            if (!firstActivation) return;
            try { build(self); } catch (const std::exception& e) { SFQ_ERR("ui build failed: %s", e.what()); }
        },
        true);
    SFQ_LOG("settings menu registered");
}

}  // namespace sfq::ui
