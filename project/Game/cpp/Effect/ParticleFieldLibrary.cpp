/**
 * @file ParticleFieldLibrary.cpp
 * @brief 名前で呼べる粒の場（ParticleFieldLibrary）の読み込みと置き方
 */
#include "ParticleFieldLibrary.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "ParticleManager.h"

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/particle_fields.json";
constexpr const char* kDirectionalType = "directional";

/** @brief particle_fields.json の1項目と調整値の対応 */
constexpr JsonField<ParticleFieldPreset> kPresetFields[] = {
    { nullptr, "type", &ParticleFieldPreset::type },
    { nullptr, "radius", &ParticleFieldPreset::radius },
    { nullptr, "strength", &ParticleFieldPreset::strength },
    { nullptr, "swirl", &ParticleFieldPreset::swirl },
    { nullptr, "drag", &ParticleFieldPreset::drag },
    { nullptr, "seconds", &ParticleFieldPreset::seconds },
    { nullptr, "height", &ParticleFieldPreset::height },
    { nullptr, "back", &ParticleFieldPreset::back },
};

/** @brief particle_fields.json から名前ごとの場を読む（_ で始まる項目は説明なので読まない） */
std::map<std::string, ParticleFieldPreset> LoadPresets() {
    std::map<std::string, ParticleFieldPreset> presets;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("ParticleField: ") + kConfigPath + " が読めません。場は出しません");
        return presets;
    }
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key().starts_with('_') || !it->is_object()) {
            continue;
        }
        presets[it.key()] = ReadJsonFields(*it, kPresetFields);
    }
    return presets;
}
} // namespace

/** @brief 読み込み済みの一覧 */
std::map<std::string, ParticleFieldPreset>& ParticleFieldLibrary::Presets() {
    static std::map<std::string, ParticleFieldPreset> sPresets = LoadPresets();
    return sPresets;
}

/** @brief particle_fields.json を読み直す */
void ParticleFieldLibrary::Reload() {
    Presets() = LoadPresets();
}

/** @brief 名前の場を置く */
void ParticleFieldLibrary::Emit(engine::graphics::ParticleManager* particles, const std::string& name, const Vector3& position, float dirX) {
    if (particles == nullptr) {
        return;
    }
    const auto& presets = Presets();
    const auto it = presets.find(name);
    if (it == presets.end()) {
        return;
    }
    const ParticleFieldPreset& preset = it->second;
    const float side = dirX >= 0.0f ? 1.0f : -1.0f;
    engine::graphics::ParticleField field;
    field.type = preset.type == kDirectionalType ? engine::graphics::ParticleFieldType::kDirectional : engine::graphics::ParticleFieldType::kRadial;
    // 突進の引きずりは体の後ろに置き、進む向きへ流す
    field.center = { position.x - side * preset.back, position.y + preset.height, position.z };
    field.direction = { side, 0.0f, 0.0f };
    field.radius = preset.radius;
    field.strength = preset.strength;
    field.swirl = preset.swirl;
    field.drag = preset.drag;
    particles->AddField(field, preset.seconds);
}

} // namespace engine::game
