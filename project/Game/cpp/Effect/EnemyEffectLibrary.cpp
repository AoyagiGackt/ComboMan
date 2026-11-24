/**
 * @file EnemyEffectLibrary.cpp
 * @brief 敵の種類ごとの攻撃とやられの演出（EnemyEffectLibrary）の読み込みと出し方
 */
#include "EnemyEffectLibrary.h"
#include "EnemyEntity.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "ParticleFieldLibrary.h"
#include "ParticleManager.h"

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/enemy_effects.json";
constexpr const char* kDefaultKind = "default"; // 種類に無い時に使う演出の名前
constexpr const char* kKnightKind = "Knight"; // モンスターでない敵（騎士）の種類の名前

/** @brief 敵1種類ぶんを JSON から読む */
EnemyEffectSet ReadSet(const std::string& kind, const nlohmann::json& json) {
    EnemyEffectSet set;
    set.color = JsonHelper::ReadColor(json.value("color", nlohmann::json()), set.color);
    set.attack = ParticleLayerList::FromJson(json.value("attack", nlohmann::json::array()), kind + ".attack");
    set.defeat = ParticleLayerList::FromJson(json.value("defeat", nlohmann::json::array()), kind + ".defeat");
    set.attackField = json.value("attackField", std::string());
    set.defeatField = json.value("defeatField", std::string());
    return set;
}

/** @brief enemy_effects.json から種類ごとの演出を読む（_ で始まる項目は説明なので読まない） */
std::map<std::string, EnemyEffectSet> LoadSets() {
    std::map<std::string, EnemyEffectSet> sets;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("EnemyEffect: ") + kConfigPath + " が読めません。敵の演出は出しません");
        return sets;
    }
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key().starts_with('_') || !it->is_object()) {
            continue;
        }
        sets.emplace(it.key(), ReadSet(it.key(), *it));
    }
    return sets;
}
} // namespace

/** @brief 読み込み済みの一覧 */
std::map<std::string, EnemyEffectSet>& EnemyEffectLibrary::Sets() {
    static std::map<std::string, EnemyEffectSet> sSets = LoadSets();
    return sSets;
}

/** @brief enemy_effects.json を読み直す */
void EnemyEffectLibrary::Reload() {
    Sets() = LoadSets();
}

/** @brief 敵の種類の名前 */
std::string EnemyEffectLibrary::KindOf(const EnemyEntity& enemy) {
    return enemy.IsMonster() ? enemy.GetMonsterKind() : std::string(kKnightKind);
}

/** @brief 種類の演出を返す */
const EnemyEffectSet* EnemyEffectLibrary::Find(const std::string& kind) {
    const auto& sets = Sets();
    auto it = sets.find(kind);
    if (it == sets.end()) {
        it = sets.find(kDefaultKind);
    }
    return it != sets.end() ? &it->second : nullptr;
}

/** @brief 層と場を出す */
void EnemyEffectLibrary::Emit(const Target& target, const EnemyEffectSet& set, const ParticleLayerList& layers,
    const std::string& field, const Vector3& position, float facingX) {
    if (target.particles == nullptr || target.rng == nullptr) {
        return;
    }
    const ElementEffectContext ctx { .particles = *target.particles, .rng = *target.rng, .facingX = facingX };
    layers.Emit(ctx, position, set.color, 1.0f);
    if (!field.empty()) {
        ParticleFieldLibrary::Emit(target.particles, field, position, facingX);
    }
}

/** @brief 攻撃を出した瞬間の演出を出す */
void EnemyEffectLibrary::EmitAttack(const Target& target, const std::string& kind, const Vector3& position, float facingX) {
    if (const EnemyEffectSet* set = Find(kind)) {
        Emit(target, *set, set->attack, set->attackField, position, facingX);
    }
}

/** @brief やられた瞬間の演出を出す */
void EnemyEffectLibrary::EmitDefeat(const Target& target, const std::string& kind, const Vector3& position) {
    if (const EnemyEffectSet* set = Find(kind)) {
        Emit(target, *set, set->defeat, set->defeatField, position, 1.0f);
    }
}

} // namespace engine::game
