/**
 * @file WeaponFinisherTuning.cpp
 * @brief 武器ごとの締めの動作の調整値（weapon_finishers.json）を読む
 */
#include "WeaponFinisherTuning.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/weapon_finishers.json";

/** @brief weapon_finishers.json の1武器ぶんの項目と調整値の対応 */
constexpr JsonField<WeaponFinisherData> kFinisherFields[] = {
    { nullptr, "launcher", &WeaponFinisherData::launcher },
    { nullptr, "guide", &WeaponFinisherData::guide },
    { nullptr, "damageMult", &WeaponFinisherData::damageMult },
    { nullptr, "length", &WeaponFinisherData::length },
    { nullptr, "radius", &WeaponFinisherData::radius },
    { nullptr, "halfHeight", &WeaponFinisherData::halfHeight },
    { nullptr, "knockX", &WeaponFinisherData::knockX },
    { nullptr, "knockY", &WeaponFinisherData::knockY },
    { nullptr, "slamSpeed", &WeaponFinisherData::slamSpeed },
    { nullptr, "stunSeconds", &WeaponFinisherData::stunSeconds },
    { nullptr, "offset", &WeaponFinisherData::offset },
    { "throw", "speed", &WeaponFinisherData::throwSpeed },
    { "throw", "distance", &WeaponFinisherData::throwDistance },
    { "throw", "spinSpeed", &WeaponFinisherData::throwSpinSpeed },
    { "throw", "hitRadius", &WeaponFinisherData::throwHitRadius },
    { "throw", "maxSeconds", &WeaponFinisherData::throwMaxSeconds },
    { "throw", "scale", &WeaponFinisherData::throwScale },
    { "effects", "impact", &WeaponFinisherData::impact },
    { "effects", "ring", &WeaponFinisherData::ring },
    { "effects", "sparks", &WeaponFinisherData::sparks },
};

/** @brief JSON に書く動作の名前と種類の対応 */
struct NamedFinishAction {
    const char* name;
    WeaponFinishAction action;
};
constexpr NamedFinishAction kActionNames[] = {
    { "none", WeaponFinishAction::kNone },
    { "warpBehind", WeaponFinishAction::kWarpBehind },
    { "thrust", WeaponFinishAction::kThrust },
    { "shockwave", WeaponFinishAction::kShockwave },
    { "slamDown", WeaponFinishAction::kSlamDown },
    { "pullSpin", WeaponFinishAction::kPullSpin },
    { "throw", WeaponFinishAction::kThrow },
};

/** @brief 動作の名前を種類へ変換する（知らない名前は何もしない動作） */
WeaponFinishAction ParseAction(const std::string& name) {
    for (const NamedFinishAction& entry : kActionNames) {
        if (name == entry.name) {
            return entry.action;
        }
    }
    Logger::LogWarning("WeaponFinisher: 知らない動作 " + name);
    return WeaponFinishAction::kNone;
}

/** @brief weapon_finishers.json から武器ごとの調整値を読む（書いていない武器は打ち上げて空中へ行く既定のまま） */
std::array<WeaponFinisherData, kWeaponTypeCount> LoadAll() {
    std::array<WeaponFinisherData, kWeaponTypeCount> result {};
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    const auto weapons = root.is_object() ? root.find("weapons") : root.end();
    if (!root.is_object() || weapons == root.end() || !weapons->is_object()) {
        Logger::LogWarning(std::string("WeaponFinisher: ") + kConfigPath + " が読めません。どの武器も打ち上げて空中へ行きます");
        return result;
    }
    for (const NamedWeaponType& entry : kWeaponTypeNames) {
        const auto found = weapons->find(entry.name);
        if (found == weapons->end() || !found->is_object()) {
            continue;
        }
        WeaponFinisherData& data = result[static_cast<size_t>(entry.type)];
        ReadJsonFields(*found, kFinisherFields, data);
        data.action = ParseAction(found->value("action", std::string("none")));
    }
    return result;
}
} // namespace

/** @brief 武器の種類の並び順に持つ調整値（初回に読み込む） */
std::array<WeaponFinisherData, kWeaponTypeCount>& WeaponFinisherTuning::Mutable() {
    static std::array<WeaponFinisherData, kWeaponTypeCount> sData = LoadAll();
    return sData;
}

/** @brief その武器の調整値を返す */
const WeaponFinisherData& WeaponFinisherTuning::Get(WeaponType type) {
    const size_t index = static_cast<size_t>(type);
    auto& all = Mutable();
    return all[index < all.size() ? index : 0];
}

/** @brief weapon_finishers.json を読み直す */
void WeaponFinisherTuning::Reload() {
    Mutable() = LoadAll();
}

} // namespace engine::game
