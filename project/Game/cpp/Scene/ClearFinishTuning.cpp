/**
 * @file ClearFinishTuning.cpp
 * @brief クリアの決め演出の調整値を clear_finish.json から読み込む
 */
#include "ClearFinishTuning.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <algorithm>
#include <string>

namespace engine::game::clearFinishTuning {
namespace {
constexpr const char* kConfigPath = "Resources/Config/clear_finish.json";
constexpr size_t kUvSize = 2; // 弾痕の位置 [u, v] の要素数

/** @brief clear_finish.json の項目と、読み込み先のメンバの対応 */
constexpr JsonField<FinishTuning> kFinishFields[] = {
    { nullptr, "dimColor", &FinishTuning::dimColor },
    { nullptr, "dimSeconds", &FinishTuning::dimSeconds },
    { "cuts", "count", &FinishTuning::cutCount },
    { "cuts", "countJitter", &FinishTuning::cutCountJitter },
    { "cuts", "interval", &FinishTuning::cutInterval },
    { "cuts", "intervalJitter", &FinishTuning::cutIntervalJitter },
    { "cuts", "spreadU", &FinishTuning::cutSpreadU },
    { "cuts", "spreadV", &FinishTuning::cutSpreadV },
    { "cuts", "seconds", &FinishTuning::cutSeconds },
    { "cuts", "thickness", &FinishTuning::cutThickness },
    { "cuts", "remainThickness", &FinishTuning::cutRemainThickness },
    { "cuts", "flash", &FinishTuning::cutFlash },
    { "cuts", "impact", &FinishTuning::cutImpact },
    { "cuts", "sound", &FinishTuning::cutSound },
    { "blade", "silenceSeconds", &FinishTuning::bladeSilenceSeconds },
    { "blade", "slashSeconds", &FinishTuning::slashSeconds },
    { "blade", "thickness", &FinishTuning::slashThickness },
    { "blade", "remainThickness", &FinishTuning::slashRemainThickness },
    { "blade", "thinSeconds", &FinishTuning::slashThinSeconds },
    { "blade", "glowScale", &FinishTuning::slashGlowScale },
    { "blade", "coreColor", &FinishTuning::slashCoreColor },
    { "blade", "glowColor", &FinishTuning::slashGlowColor },
    { "blade", "holdSeconds", &FinishTuning::slashHoldSeconds },
    { "blade", "angleJitter", &FinishTuning::slashAngleJitter },
    { "blade", "offsetJitter", &FinishTuning::slashOffsetJitter },
    { "blade", "flash", &FinishTuning::slashFlash },
    { "blade", "impact", &FinishTuning::slashImpact },
    { "blade", "sound", &FinishTuning::slashSound },
    { "spray", "count", &FinishTuning::sprayCount },
    { "spray", "countJitter", &FinishTuning::sprayCountJitter },
    { "spray", "interval", &FinishTuning::sprayInterval },
    { "spray", "intervalJitter", &FinishTuning::sprayIntervalJitter },
    { "spray", "margin", &FinishTuning::sprayMargin },
    { "spray", "holeScale", &FinishTuning::sprayHoleScale },
    { "spray", "flash", &FinishTuning::sprayFlash },
    { "spray", "impact", &FinishTuning::sprayImpact },
    { "spray", "sound", &FinishTuning::spraySound },
    { "gun", "silenceSeconds", &FinishTuning::gunSilenceSeconds },
    { "gun", "interval", &FinishTuning::shotInterval },
    { "gun", "holeSize", &FinishTuning::holeSize },
    { "gun", "popScale", &FinishTuning::holePopScale },
    { "gun", "popSeconds", &FinishTuning::holePopSeconds },
    { "gun", "holeColor", &FinishTuning::holeColor },
    { "gun", "sparkScale", &FinishTuning::sparkScale },
    { "gun", "sparkSeconds", &FinishTuning::sparkSeconds },
    { "gun", "sparkColor", &FinishTuning::sparkColor },
    { "gun", "holdSeconds", &FinishTuning::shotHoldSeconds },
    { "gun", "shotJitter", &FinishTuning::shotJitter },
    { "gun", "flash", &FinishTuning::shotFlash },
    { "gun", "impact", &FinishTuning::shotImpact },
    { "gun", "sound", &FinishTuning::shotSound },
    { "shatter", "seconds", &FinishTuning::shatterSeconds },
    { "shatter", "crackWidth", &FinishTuning::crackWidth },
    { "shatter", "shardSpeed", &FinishTuning::shardSpeed },
    { nullptr, "soundVolume", &FinishTuning::soundVolume },
};

/** @brief blade.slashes（{ angleDegrees, delay } の配列）を読む（無ければ fallback を返す） */
std::vector<FinishSlash> ReadSlashes(const nlohmann::json& root, const std::vector<FinishSlash>& fallback) {
    const auto blade = root.find("blade");
    if (blade == root.end() || !blade->contains("slashes") || !(*blade)["slashes"].is_array()) {
        return fallback;
    }
    std::vector<FinishSlash> slashes;
    for (const auto& item : (*blade)["slashes"]) {
        FinishSlash slash;
        slash.angleDegrees = item.value("angleDegrees", slash.angleDegrees);
        slash.delay = item.value("delay", slash.delay);
        slashes.push_back(slash);
    }
    return slashes.empty() ? fallback : slashes;
}

/** @brief gun.shots（画面上の位置 [u, v] の配列、0〜1）を読む（無ければ fallback を返す） */
std::vector<Vector2> ReadShots(const nlohmann::json& root, const std::vector<Vector2>& fallback) {
    const auto gun = root.find("gun");
    if (gun == root.end() || !gun->contains("shots") || !(*gun)["shots"].is_array()) {
        return fallback;
    }
    std::vector<Vector2> shots;
    for (const auto& item : (*gun)["shots"]) {
        // 数字でない組は読まない（そのまま取り出すと例外で止まる）
        if (item.is_array() && item.size() >= kUvSize && item[0].is_number() && item[1].is_number()) {
            shots.push_back({ item[0].get<float>(), item[1].get<float>() });
        }
    }
    return shots.empty() ? fallback : shots;
}

/** @brief clear_finish.json からクリアの決め演出の設定を読み込む */
FinishConfig LoadConfig() {
    FinishConfig config;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("ClearFinish: ") + kConfigPath + " が読めません。既定値で動きます");
        return config;
    }
    ReadJsonFields(root, kFinishFields, config.tuning);
    config.slashes = ReadSlashes(root, config.slashes);
    config.shots = ReadShots(root, config.shots);
    // 本数は負にしない（スプライトを最初にこの数だけ作る）
    config.tuning.cutCount = (std::max)(config.tuning.cutCount, 0);
    config.tuning.sprayCount = (std::max)(config.tuning.sprayCount, 0);
    // 減らす数は 0〜本数 の範囲に収める
    config.tuning.cutCountJitter = std::clamp(config.tuning.cutCountJitter, 0, config.tuning.cutCount);
    config.tuning.sprayCountJitter = std::clamp(config.tuning.sprayCountJitter, 0, config.tuning.sprayCount);
    return config;
}
} // namespace

/** @brief 読み込み済みの設定（初回に読む。変更は次回起動時に反映） */
const FinishConfig& Config() {
    static const FinishConfig kConfig = LoadConfig();
    return kConfig;
}

/** @brief 読み込み済みの調整値を取得する */
const FinishTuning& Tune() {
    return Config().tuning;
}
} // namespace engine::game::clearFinishTuning
