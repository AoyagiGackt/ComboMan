/**
 * @file BossExecutionTuning.cpp
 * @brief 膝をつく姿勢と、とどめの流れの調整値（BossExecutionTuning）を boss_execution.json から読み込む
 */
#include "BossExecutionTuning.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/boss_execution.json";
constexpr const char* kJsonExtension = ".json";

/** @brief ルート直下の共通の値 */
constexpr JsonField<BossExecutionTuning> kRootFields[] = {
    { nullptr, "bossCenterHeight", &BossExecutionTuning::bossCenterHeight },
    { nullptr, "cameraHeight", &BossExecutionTuning::cameraHeight },
    { nullptr, "zoomRate", &BossExecutionTuning::zoomRate },
    { nullptr, "slashMarkColor", &BossExecutionTuning::slashMarkColor },
    { nullptr, "slashMarkThickness", &BossExecutionTuning::slashMarkThickness },
    { nullptr, "slashMarkSeconds", &BossExecutionTuning::slashMarkSeconds },
    { nullptr, "defaultStyle", &BossExecutionTuning::defaultStyle },
    { nullptr, "gunDefaultStyle", &BossExecutionTuning::gunDefaultStyle },
    { nullptr, "bombStyle", &BossExecutionTuning::bombStyle },
};

/** @brief kneel の中の値 */
constexpr JsonField<BossKneelTuning> kKneelFields[] = {
    { nullptr, "sink", &BossKneelTuning::sink },
    { nullptr, "bodyLean", &BossKneelTuning::bodyLean },
    { nullptr, "breathAmount", &BossKneelTuning::breathAmount },
    { nullptr, "breathSpeed", &BossKneelTuning::breathSpeed },
    { nullptr, "animSpeed", &BossKneelTuning::animSpeed },
    { nullptr, "weaponOffset", &BossKneelTuning::weaponOffset },
    { nullptr, "weaponTilt", &BossKneelTuning::weaponTilt },
    { nullptr, "weaponDimColor", &BossKneelTuning::weaponDimColor },
    { nullptr, "weaponPulseSpeed", &BossKneelTuning::weaponPulseSpeed },
};

/** @brief kneel.reaches の1件 */
constexpr JsonField<KneelReach> kReachFields[] = {
    { nullptr, "bone", &KneelReach::bone },
    { nullptr, "direction", &KneelReach::direction },
    { nullptr, "weight", &KneelReach::weight },
};

/** @brief 拍1つ */
constexpr JsonField<ExecutionBeat> kBeatFields[] = {
    { nullptr, "delay", &ExecutionBeat::delay },
    { nullptr, "afterMove", &ExecutionBeat::afterMove },
    { nullptr, "move", &ExecutionBeat::move },
    { nullptr, "moveTo", &ExecutionBeat::moveTo },
    { nullptr, "moveSeconds", &ExecutionBeat::moveSeconds },
    { nullptr, "walkSpeed", &ExecutionBeat::walkSpeed },
    { nullptr, "face", &ExecutionBeat::face },
    { nullptr, "pose", &ExecutionBeat::pose },
    { nullptr, "poseSeconds", &ExecutionBeat::poseSeconds },
    { nullptr, "shot", &ExecutionBeat::shot },
    { nullptr, "grab", &ExecutionBeat::grab },
    { nullptr, "slashAngle", &ExecutionBeat::slashAngle },
    { nullptr, "slashLength", &ExecutionBeat::slashLength },
    { nullptr, "slashOffset", &ExecutionBeat::slashOffset },
    { nullptr, "screenSlash", &ExecutionBeat::screenSlash },
    { nullptr, "screenSlashLength", &ExecutionBeat::screenSlashLength },
    { nullptr, "hitStop", &ExecutionBeat::hitStop },
    { nullptr, "shake", &ExecutionBeat::shake },
    { nullptr, "shakeSeconds", &ExecutionBeat::shakeSeconds },
    { nullptr, "sparks", &ExecutionBeat::sparks },
    { nullptr, "ring", &ExecutionBeat::ring },
    { nullptr, "flash", &ExecutionBeat::flash },
    { nullptr, "impact", &ExecutionBeat::impact },
    { nullptr, "playerSparks", &ExecutionBeat::playerSparks },
    { nullptr, "sound", &ExecutionBeat::sound },
    { nullptr, "cue", &ExecutionBeat::cue },
    { nullptr, "slowSeconds", &ExecutionBeat::slowSeconds },
    { nullptr, "slowScale", &ExecutionBeat::slowScale },
    { nullptr, "zoom", &ExecutionBeat::zoom },
    { nullptr, "slice", &ExecutionBeat::slice },
    { nullptr, "bomb", &ExecutionBeat::bomb },
};

/** @brief 拍の配列を読む（配列でなければ空のまま） */
std::vector<ExecutionBeat> ReadBeats(const nlohmann::json& array) {
    std::vector<ExecutionBeat> beats;
    if (!array.is_array()) {
        return beats;
    }
    for (const nlohmann::json& item : array) {
        beats.push_back(ReadJsonFields(item, kBeatFields));
    }
    return beats;
}

/** @brief ステージのファイル名から型の名前への割り当て表を読む（オブジェクトでなければ空のまま） */
std::unordered_map<std::string, std::string> ReadStageTable(const nlohmann::json& root, const char* key) {
    std::unordered_map<std::string, std::string> table;
    const auto stages = root.find(key);
    if (stages == root.end() || !stages->is_object()) {
        return table;
    }
    for (auto it = stages->begin(); it != stages->end(); ++it) {
        if (it.value().is_string()) {
            table[it.key()] = it.value().get<std::string>();
        }
    }
    return table;
}

/** @brief boss_execution.json から調整値を読み込む */
BossExecutionTuning LoadTuning() {
    BossExecutionTuning tuning;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("BossExecution: ") + kConfigPath + " が読めません。既定値で動きます");
        return tuning;
    }
    ReadJsonFields(root, kRootFields, tuning);
    // 膝をつく姿勢と、曲げるボーンの一覧
    const auto kneel = root.find("kneel");
    if (kneel != root.end() && kneel->is_object()) {
        ReadJsonFields(*kneel, kKneelFields, tuning.kneel);
        const auto reaches = kneel->find("reaches");
        if (reaches != kneel->end() && reaches->is_array()) {
            for (const nlohmann::json& item : *reaches) {
                tuning.kneel.reaches.push_back(ReadJsonFields(item, kReachFields));
            }
        }
    }
    // どの型でも最初に行う拍
    const auto intro = root.find("intro");
    if (intro != root.end()) {
        tuning.intro = ReadBeats(*intro);
    }
    // 銃で倒した時に最初に行う拍
    const auto gunIntro = root.find("gunIntro");
    if (gunIntro != root.end()) {
        tuning.gunIntro = ReadBeats(*gunIntro);
    }
    // 爆弾がめり込んだまま倒した時に最初に行う拍
    const auto bombIntro = root.find("bombIntro");
    if (bombIntro != root.end()) {
        tuning.bombIntro = ReadBeats(*bombIntro);
    }
    // ステージごとの割り当て（斬る型と、銃で倒した時の型）
    tuning.stages = ReadStageTable(root, "stages");
    tuning.gunStages = ReadStageTable(root, "gunStages");
    // 型の一覧
    const auto styles = root.find("styles");
    if (styles != root.end() && styles->is_object()) {
        for (auto it = styles->begin(); it != styles->end(); ++it) {
            if (!it.value().is_object()) {
                continue;
            }
            ExecutionStyle style;
            const auto endHold = it.value().find("endHold");
            if (endHold != it.value().end() && endHold->is_number()) {
                style.endHold = endHold->get<float>();
            }
            const auto beats = it.value().find("beats");
            if (beats != it.value().end()) {
                style.beats = ReadBeats(*beats);
            }
            tuning.styles[it.key()] = std::move(style);
        }
    }
    return tuning;
}

/** @brief レベルのパスからファイル名（拡張子なし）を取り出す */
std::string LevelStem(const std::string& levelPath) {
    const size_t slash = levelPath.find_last_of("/\\");
    std::string stem = slash == std::string::npos ? levelPath : levelPath.substr(slash + 1);
    const size_t extension = stem.rfind(kJsonExtension);
    if (extension != std::string::npos) {
        stem.erase(extension);
    }
    return stem;
}

/** @brief 割り当て表からステージの型を引く（割り当てが無ければ fallback の名前の型、それも無ければ nullptr） */
const ExecutionStyle* LookupStyle(const std::unordered_map<std::string, ExecutionStyle>& styles,
    const std::unordered_map<std::string, std::string>& table, const std::string& fallback, const std::string& levelPath) {
    const auto stage = table.find(LevelStem(levelPath));
    const std::string& name = stage != table.end() ? stage->second : fallback;
    const auto style = styles.find(name);
    return style != styles.end() ? &style->second : nullptr;
}
} // namespace

/** @brief ステージのレベルのパスに割り当てた型を返す（無ければ既定の型、それも無ければ nullptr） */
const ExecutionStyle* BossExecutionTuning::FindStyle(const std::string& levelPath) const {
    return LookupStyle(styles, stages, defaultStyle, levelPath);
}

/** @brief 銃で倒した時の型を返す（ステージの割り当てが無ければ銃の既定の型、それも無ければ nullptr） */
const ExecutionStyle* BossExecutionTuning::FindGunStyle(const std::string& levelPath) const {
    return LookupStyle(styles, gunStages, gunDefaultStyle, levelPath);
}

/** @brief ボスに爆弾がめり込んだまま倒した時の型を返す（無ければ nullptr） */
const ExecutionStyle* BossExecutionTuning::FindBombStyle() const {
    if (bombStyle.empty()) {
        return nullptr;
    }
    const auto style = styles.find(bombStyle);
    return style != styles.end() ? &style->second : nullptr;
}

/** @brief 値の実体（初回に読み込む） */
BossExecutionTuning& BossExecutionTuningStore::Mutable() {
    static BossExecutionTuning sTuning = LoadTuning();
    return sTuning;
}

/** @brief 読み込み済みの値を返す */
const BossExecutionTuning& BossExecutionTuningStore::Get() {
    return Mutable();
}

/** @brief boss_execution.json を読み直す */
void BossExecutionTuningStore::Reload() {
    Mutable() = LoadTuning();
}

} // namespace engine::game
