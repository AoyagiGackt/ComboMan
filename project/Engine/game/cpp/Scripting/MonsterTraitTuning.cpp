/**
 * @file MonsterTraitTuning.cpp
 * @brief モンスターごとの個性の動きの調整値（monster_traits.json）を読む
 */
#include "MonsterTraitTuning.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/monster_traits.json";

/** @brief monster_traits.json の1種類ぶんの項目と調整値の対応 */
constexpr JsonField<MonsterTraitData> kTraitFields[] = {
    { nullptr, "interval", &MonsterTraitData::interval },
    { nullptr, "range", &MonsterTraitData::range },
    { nullptr, "telegraphSeconds", &MonsterTraitData::telegraphSeconds },
    { nullptr, "damage", &MonsterTraitData::damage },
    { nullptr, "count", &MonsterTraitData::count },
    { nullptr, "spreadDegrees", &MonsterTraitData::spreadDegrees },
    { nullptr, "speed", &MonsterTraitData::speed },
    { nullptr, "riseSpeed", &MonsterTraitData::riseSpeed },
    { nullptr, "gravity", &MonsterTraitData::gravity },
    { nullptr, "lifetime", &MonsterTraitData::lifetime },
    { nullptr, "launchHeight", &MonsterTraitData::launchHeight },
    { nullptr, "hopX", &MonsterTraitData::hopX },
    { nullptr, "hopY", &MonsterTraitData::hopY },
    { nullptr, "swoopSeconds", &MonsterTraitData::swoopSeconds },
    { nullptr, "returnSeconds", &MonsterTraitData::returnSeconds },
    { "effects", "warningRing", &MonsterTraitData::warningRing },
    { "effects", "actSparks", &MonsterTraitData::actSparks },
    { "effects", "attackSparks", &MonsterTraitData::attackSparks },
    { "effects", "attackRing", &MonsterTraitData::attackRing },
    { "effects", "attackDrops", &MonsterTraitData::attackDrops },
    { "effects", "landSparks", &MonsterTraitData::landSparks },
    { "effects", "landRing", &MonsterTraitData::landRing },
};

/** @brief JSON に書く動きの名前と種類の対応 */
struct NamedTrait {
    const char* name;
    MonsterTrait trait;
};
constexpr NamedTrait kTraitNames[] = {
    { "none", MonsterTrait::kNone },
    { "fireBreath", MonsterTrait::kFireBreath },
    { "boneThrow", MonsterTrait::kBoneThrow },
    { "hop", MonsterTrait::kHop },
    { "swoop", MonsterTrait::kSwoop },
};

/** @brief 動きの名前を種類へ変換する（知らない名前は個性なし） */
MonsterTrait ParseTrait(const std::string& name) {
    for (const NamedTrait& entry : kTraitNames) {
        if (name == entry.name) {
            return entry.trait;
        }
    }
    Logger::LogWarning("MonsterTrait: 知らない動き " + name);
    return MonsterTrait::kNone;
}

/** @brief monster_traits.json からモンスターごとの個性を読む */
std::map<std::string, MonsterTraitData> LoadAll() {
    std::map<std::string, MonsterTraitData> result;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    const auto monsters = root.is_object() ? root.find("monsters") : root.end();
    if (!root.is_object() || monsters == root.end() || !monsters->is_object()) {
        Logger::LogWarning(std::string("MonsterTrait: ") + kConfigPath + " が読めません。モンスターは個性の動きをしません");
        return result;
    }
    for (auto it = monsters->begin(); it != monsters->end(); ++it) {
        if (!it->is_object()) {
            continue;
        }
        MonsterTraitData data;
        ReadJsonFields(*it, kTraitFields, data);
        data.trait = ParseTrait(it->value("trait", std::string("none")));
        result[it.key()] = data;
    }
    return result;
}
} // namespace

/** @brief モンスターの種類の名前ごとの調整値（初回に読み込む） */
std::map<std::string, MonsterTraitData>& MonsterTraitTuning::Mutable() {
    static std::map<std::string, MonsterTraitData> sData = LoadAll();
    return sData;
}

/** @brief モンスターの種類の個性を返す（書いていない種類は個性なし） */
const MonsterTraitData& MonsterTraitTuning::Get(const std::string& monsterKind) {
    static const MonsterTraitData kNoTrait {};
    const auto& all = Mutable();
    const auto found = all.find(monsterKind);
    return found != all.end() ? found->second : kNoTrait;
}

/** @brief monster_traits.json を読み直す */
void MonsterTraitTuning::Reload() {
    Mutable() = LoadAll();
}

} // namespace engine::game
