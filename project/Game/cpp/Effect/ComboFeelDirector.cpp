/**
 * @file ComboFeelDirector.cpp
 * @brief ComboFeelDirectorの刃の軌跡サンプリング、締め演出の時間管理、斬撃線の向きの決定を実装するファイル（調整パネルは ComboFeelDirectorPanel.cpp）
 */
#include "ComboFeelDirector.h"
#include "Camera.h"
#include "GameConstants.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "MeleeCombo.h"
#include "Player.h"
#include "SceneShared.h"
#include "TimeManager.h"
#include "Weapon.h"
#include <algorithm>
#include <chrono>
#include <cmath>
using engine::graphics::Camera;
using engine::graphics::SrvManager;
using engine::graphics::WeaponTrailStyle;

namespace engine::game {
namespace {

constexpr const char* kSettingsPath = ComboFeelSettings::kFilePath;

// combo_feel.json の1つの値で済む項目（刃の範囲は武器種ごとの表なので個別に読み書きする）
constexpr JsonField<ComboFeelSettings> kSettingsFields[] = {
    { nullptr, "trailEnabled", &ComboFeelSettings::trailEnabled },
    { nullptr, "trailSubdivisions", &ComboFeelSettings::trailSubdivisions },
    { nullptr, "trailLifetime", &ComboFeelSettings::trailLifetime },
    { nullptr, "trailTipAlpha", &ComboFeelSettings::trailTipAlpha },
    { nullptr, "trailBaseAlpha", &ComboFeelSettings::trailBaseAlpha },
    { nullptr, "trailTint", &ComboFeelSettings::trailTint },
    { nullptr, "impactEnabled", &ComboFeelSettings::impactEnabled },
    { nullptr, "radialBlurEnabled", &ComboFeelSettings::radialBlurEnabled },
    { nullptr, "blurPeak", &ComboFeelSettings::blurPeak },
    { nullptr, "blurSeconds", &ComboFeelSettings::blurSeconds },
    { nullptr, "blurSamples", &ComboFeelSettings::blurSamples },
    { nullptr, "zoomEnabled", &ComboFeelSettings::zoomEnabled },
    { nullptr, "zoomDistance", &ComboFeelSettings::zoomDistance },
    { nullptr, "zoomSeconds", &ComboFeelSettings::zoomSeconds },
    { nullptr, "slowEnabled", &ComboFeelSettings::slowEnabled },
    { nullptr, "slowScale", &ComboFeelSettings::slowScale },
    { nullptr, "slowSeconds", &ComboFeelSettings::slowSeconds },
    { nullptr, "slashEnabled", &ComboFeelSettings::slashEnabled },
    { nullptr, "slashLength", &ComboFeelSettings::slashLength },
    { nullptr, "slashThickness", &ComboFeelSettings::slashThickness },
    { nullptr, "finisherSlashThickness", &ComboFeelSettings::finisherSlashThickness },
    { nullptr, "finisherSlashScale", &ComboFeelSettings::finisherSlashScale },
    { nullptr, "slashSeconds", &ComboFeelSettings::slashSeconds },
    { nullptr, "slashDirectionWindow", &ComboFeelSettings::slashDirectionWindow },
    { nullptr, "awakenEnabled", &ComboFeelSettings::awakenEnabled },
    { nullptr, "awakenColor", &ComboFeelSettings::awakenColor },
    { nullptr, "awakenTint", &ComboFeelSettings::awakenTint },
    { nullptr, "awakenTrailLifetimeScale", &ComboFeelSettings::awakenTrailLifetimeScale },
    { nullptr, "awakenGlowExtend", &ComboFeelSettings::awakenGlowExtend },
    { nullptr, "awakenGlowAlpha", &ComboFeelSettings::awakenGlowAlpha },
    { nullptr, "awakenSlashCount", &ComboFeelSettings::awakenSlashCount },
    { nullptr, "awakenSlashSpreadDeg", &ComboFeelSettings::awakenSlashSpreadDeg },
    { nullptr, "awakenImpactScale", &ComboFeelSettings::awakenImpactScale },
};

// combo_feel.json の weaponFeel の武器ごとの項目
constexpr JsonField<WeaponFeel> kWeaponFeelFields[] = {
    { nullptr, "trailLifetimeScale", &WeaponFeel::trailLifetimeScale },
    { nullptr, "trailAlphaScale", &WeaponFeel::trailAlphaScale },
    { nullptr, "slashLengthScale", &WeaponFeel::slashLengthScale },
    { nullptr, "slashThicknessScale", &WeaponFeel::slashThicknessScale },
    { nullptr, "slashSecondsScale", &WeaponFeel::slashSecondsScale },
    { nullptr, "slashExtraLines", &WeaponFeel::slashExtraLines },
    { nullptr, "slashLineSpread", &WeaponFeel::slashLineSpread },
    { nullptr, "impactScale", &WeaponFeel::impactScale },
    { nullptr, "slowSecondsScale", &WeaponFeel::slowSecondsScale },
    { nullptr, "trailTexture", &WeaponFeel::trailTexture },
    { nullptr, "trailIntensity", &WeaponFeel::trailIntensity },
    { nullptr, "trailTextureRepeat", &WeaponFeel::trailTextureRepeat },
    { nullptr, "trailTextureScroll", &WeaponFeel::trailTextureScroll },
    { nullptr, "finisherEnemyStopFrames", &WeaponFeel::finisherEnemyStopFrames },
};

// WeaponType の並び順の武器の名前（パネルとJSONのキーに使う）
constexpr const auto& kBladeWeaponNames = ComboFeelSettings::kBladeWeaponNames;

constexpr float kMinClipW = 0.0001f; // 投影したw成分がこれ以下ならカメラの後ろとみなす
constexpr float kScreenCenterUV = 0.5f; // 画面中央のUV
constexpr float kMinSlashMotion = 0.05f; // 刃先の移動がこれ未満なら向きの推定に使わない
constexpr float kFallbackDiagonalRise = 0.55f; // 横振りの推定向きの傾き（袈裟斬りらしく斜めにする）
constexpr float kFallbackVerticalLean = 0.25f; // 縦振りの推定向きの前傾き
constexpr float kSlashWhiteMix = 0.55f; // 斬撃線の色を白へ寄せる割合
constexpr float kSecondSlashOffset = 0.12f; // 2本目の細い斬撃線をずらす距離（ワールド単位）
constexpr float kSecondSlashThicknessRatio = 0.35f; // 2本目の太さ（1本目に対する比）
constexpr float kSecondSlashLengthRatio = 0.8f; // 2本目の長さ（1本目に対する比）
constexpr float kZoomAttackRatio = 0.18f; // ズームの所要時間のうち寄り切るまでの割合（残りで戻る）
constexpr float kMicrosPerSecond = 1000000.0f;
constexpr int kMaxSlashExtraLines = ComboFeelSettings::kMaxSlashExtraLines; // 斬撃線に平行に重ねる細い線の上限

/** @brief 2色を割合 t で混ぜた色を返す */
Vector4 MixColor(const Vector4& a, const Vector4& b, float t) {
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
}

/** @brief Vector3 を3要素のJSON配列へ変換する */
nlohmann::json ToJson(const Vector3& v) {
    return nlohmann::json::array({ v.x, v.y, v.z });
}

/** @brief 数値の配列なら要素数ぶん読み出す（形が違えばfalse） */
bool FloatsFromJson(const nlohmann::json& j, float* out, size_t count) {
    if (!j.is_array() || j.size() != count) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!j[i].is_number()) {
            return false;
        }
        out[i] = j[i].get<float>();
    }
    return true;
}

/** @brief 3要素のJSON配列から Vector3 を読む（形が違えば既定値を返す） */
Vector3 Vector3FromJson(const nlohmann::json& j, const Vector3& fallback) {
    constexpr size_t kVector3Size = 3;
    Vector3 v = fallback;
    return FloatsFromJson(j, &v.x, kVector3Size) ? v : fallback;
}


} // namespace

/** @brief 演出設定を既定値で初期化する */
ComboFeelSettings::ComboFeelSettings() {
    // WeaponTypeの並びに合わせたモデル空間の根元と先端。
    static constexpr std::array<BladeRange, kWeaponTypeCount> kDefaultBlades = {{
        { { 0.0f, 0.3f, 0.0f }, { 0.0f, 4.35f, 0.0f } },
        { { 0.0f, 7.6f, 0.0f }, { 0.0f, 9.7f, 0.0f } },
        { { 0.0f, 3.3f, 0.0f }, { 0.0f, 4.33f, 0.0f } },
        { { 0.0f, 1.0f, 0.0f }, { 0.0f, 2.6f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
        { { 0.0f, 1.2f, 0.0f }, { 0.0f, 6.6f, 0.0f } },
        { { 0.3f, 2.2f, 0.0f }, { 2.2f, 2.0f, 0.0f } },
        { { 0.6f, 4.4f, 0.0f }, { 0.6f, 6.35f, 0.0f } },
    }};
    blades = kDefaultBlades;

    // WeaponTypeの並びに合わせた武器ごとの手応え
    // 刀: 細く長い一閃と長めの間 / 槍: 細く長い突きの線 / ハンマー: 太く短い線と重い締め / 短剣: 短い軌跡と平行な刻み跡
    // 大剣: 太く残る軌跡と重い締め / 鎌: 長く尾を引く三日月と二重の線 / 斧: 太い線と重い締め
    // 軌跡の筋は 刀=炎（後ろへ流れる）/ 槍=稲妻（速くちらつく）/ ハンマー=重力の縞 / 短剣=氷の細い線 / 大剣=血の太い帯 / 鎌=虚無の煙 / 斧=風の斜めの筋
    static const std::array<WeaponFeel, kWeaponTypeCount> kDefaultFeels = {{
        // 軌跡の長さ, 濃さ, 線の長さ, 太さ, 残る長さ, 重ねる線, 線の間, 締めの強さ, スローの長さ, 筋の画像, 明るさ, 筋の繰り返し, 筋の流れ, 締めで敵だけ止める
        // 刀は居合のように斬った後しばらく敵だけが止まり、重い武器は少しだけ、速い武器は止めない
        { 0.8f, 1.1f, 1.35f, 0.8f, 1.2f, 0, 0.0f, 1.0f, 1.3f, "Resources/Effects/trails/trail_fire.png", 1.6f, 1.5f, -2.0f, 16 },
        { 0.7f, 0.9f, 1.5f, 0.6f, 1.0f, 0, 0.0f, 0.9f, 0.8f, "Resources/Effects/trails/trail_lightning.png", 1.8f, 2.0f, 6.0f, 0 },
        { 1.3f, 0.8f, 0.7f, 1.8f, 1.1f, 0, 0.0f, 1.6f, 1.2f, "Resources/Effects/trails/trail_gravity.png", 1.2f, 1.0f, 1.0f, 10 },
        { 0.6f, 1.0f, 0.7f, 0.6f, 0.9f, 2, 0.14f, 0.7f, 0.6f, "Resources/Effects/trails/trail_ice.png", 1.5f, 2.5f, 0.0f, 0 },
        { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 0.0f, 1.0f, 1.0f, "", 1.0f, 1.0f, 0.0f, 0 },
        { 1.4f, 1.0f, 1.3f, 1.6f, 1.2f, 1, 0.2f, 1.4f, 1.3f, "Resources/Effects/trails/trail_blood.png", 1.3f, 1.0f, -0.5f, 8 },
        { 1.8f, 0.9f, 1.2f, 0.9f, 1.4f, 1, 0.22f, 1.1f, 1.0f, "Resources/Effects/trails/trail_void.png", 1.4f, 1.2f, 1.5f, 4 },
        { 1.1f, 1.0f, 0.9f, 1.5f, 1.1f, 0, 0.0f, 1.5f, 1.1f, "Resources/Effects/trails/trail_wind.png", 1.4f, 1.5f, -3.0f, 8 },
    }};
    weaponFeels = kDefaultFeels;
}

// ══════════════════════════════════════════════════════
// 初期化・終了
// ══════════════════════════════════════════════════════

/** @brief 刃の軌跡・光る軌跡・ブラーを初期化し、演出設定を JSON から読み込む */
void ComboFeelDirector::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager) {
    trail_.Initialize(dxCommon);
    glowTrail_.Initialize(dxCommon);
    blur_.Initialize(dxCommon, srvManager);
    LoadSettings();
    // 武器ごとの筋の画像は、振った瞬間に読み込みで止まらないよう先に読んでおく
    for (const WeaponFeel& feel : settings_.weaponFeels) {
        engine::graphics::WeaponTrailEffect::PreloadTexture(feel.trailTexture);
    }
    lastAttack_ = nullptr;
    impactTimer_ = -1.0f;
}

/** @brief 演出の状態を消す（シーン終了時に呼ぶ） */
void ComboFeelDirector::Finalize() {
    impactTimer_ = -1.0f;
    blur_.SetParams(kScreenCenterUV, kScreenCenterUV, 0.0f, settings_.blurSamples);
    trail_.Clear();
    glowTrail_.Clear();
}

// ══════════════════════════════════════════════════════
// 毎フレームの更新
// ══════════════════════════════════════════════════════

/** @brief 刃の位置を軌跡に積み、締め演出の時間を進める（毎フレーム呼ぶ） */
void ComboFeelDirector::Update(const Player& player, const Vector4& weaponColor, Camera* camera, bool gameAdvanced) {
    lastPlayerPos_ = player.GetPosition();

    const auto begin = std::chrono::high_resolution_clock::now();
    // ヒットストップやスローで止まったフレームは刃も止まっているので、積まずに帯ごと止める
    if (gameAdvanced) {
        SampleBlade(player);
    }
    // 色と寿命は刃を積んだ後に決める（持ち替えた直後の振りでも、今の武器の手応えで描くように）
    ApplyTrailStyle(weaponColor);
    const float trailDt = gameAdvanced ? gameConstants::kFrameDeltaTime : 0.0f;
    trail_.Update(trailDt, camera);
    glowTrail_.Update(trailDt, camera);
    const auto end = std::chrono::high_resolution_clock::now();
    lastTrailMicros_ = std::chrono::duration<float>(end - begin).count() * kMicrosPerSecond;

    UpdateImpact(camera);
}

/** @brief 手に持っている武器の刃の根元と先端をワールド座標で返す（持っていなければ false） */
bool ComboFeelDirector::GetBladeWorld(const Player& player, Vector3& outBase, Vector3& outTip) const {
    WeaponType type { };
    Matrix4x4 weaponMatrix { };
    if (!player.GetHeldWeaponTransform(type, weaponMatrix)) {
        return false;
    }
    const size_t index = static_cast<size_t>(type);
    if (index >= settings_.blades.size()) {
        return false;
    }
    outBase = TransformPoint(settings_.blades[index].base, weaponMatrix);
    outTip = TransformPoint(settings_.blades[index].tip, weaponMatrix);
    return true;
}

/** @brief 刃の範囲を武器行列でワールドへ移して軌跡に積む */
void ComboFeelDirector::SampleBlade(const Player& player) {
    // 近接の段が出ていない・武器を持っていない間は帯を途切れさせて何も積まない
    const MeleeAttackDef* attack = player.GetActiveMeleeAttack();
    WeaponType type { };
    Matrix4x4 weaponMatrix { };
    if (attack == nullptr || !player.GetHeldWeaponTransform(type, weaponMatrix)) {
        if (lastAttack_ != nullptr) {
            trail_.BreakStroke();
            glowTrail_.BreakStroke();
        }
        lastAttack_ = nullptr;
        return;
    }
    // 段が切り替わったら前の振りと帯を繋がない（振り抜きと次の振りかぶりの間に帯が渡らないように）
    if (attack != lastAttack_) {
        trail_.BreakStroke();
        glowTrail_.BreakStroke();
    }
    lastAttack_ = attack;

    // 武器ごとの刃の根元と先端を、今の武器の行列でワールド座標へ移して帯に積む
    const size_t index = static_cast<size_t>(type);
    if (index >= settings_.blades.size()) {
        return;
    }
    lastWeaponIndex_ = static_cast<int>(index);
    heldWeaponIndex_ = static_cast<int>(index);
    const BladeRange& blade = settings_.blades[index];
    const Vector3 base = TransformPoint(blade.base, weaponMatrix);
    const Vector3 tip = TransformPoint(blade.tip, weaponMatrix);
    trail_.AddSample(base, tip);

    // 外側の光の帯は刃の中ほどから刃先の先まで（覚醒していない間も積んでおき、覚醒した瞬間から途切れず出す）
    constexpr float kGlowStartRatio = 0.5f; // 光の帯を刃のどこから始めるか（根元0〜刃先1）
    const Vector3 blade3 = { tip.x - base.x, tip.y - base.y, tip.z - base.z };
    const Vector3 glowBase = base + blade3 * kGlowStartRatio;
    const Vector3 glowTip = tip + blade3 * settings_.awakenGlowExtend;
    glowTrail_.AddSample(glowBase, glowTip);
}

/** @brief 軌跡の色を設定値と武器の色から作る */
void ComboFeelDirector::ApplyTrailStyle(const Vector4& weaponColor) {
    constexpr Vector4 kWhite = { 1.0f, 1.0f, 1.0f, 1.0f };
    const bool boosted = IsAwakenBoosted();
    const WeaponFeel& feel = CurrentFeel();
    const float lifetime = settings_.trailLifetime * feel.trailLifetimeScale * (boosted ? settings_.awakenTrailLifetimeScale : 1.0f);
    // 覚醒中は根元の色も覚醒色へ寄せ、全体を一色の光の刃に見せる
    const Vector4 bodyColor = boosted ? MixColor(weaponColor, settings_.awakenColor, settings_.awakenTint) : weaponColor;

    WeaponTrailStyle style;
    style.lifetime = lifetime;
    style.subdivisions = settings_.trailSubdivisions;
    // 刃先は白に近い芯、根元は武器の色で透けていく
    style.tipColor = MixColor(kWhite, bodyColor, settings_.trailTint);
    style.tipColor.w = (std::min)(settings_.trailTipAlpha * feel.trailAlphaScale, 1.0f);
    style.baseColor = { bodyColor.x, bodyColor.y, bodyColor.z, (std::min)(settings_.trailBaseAlpha * feel.trailAlphaScale, 1.0f) };
    // 武器ごとの筋の画像と明るさ（芯が白く飛んだ所がブルームで光る）
    style.texture = feel.trailTexture;
    style.intensity = feel.trailIntensity;
    style.textureRepeat = feel.trailTextureRepeat;
    style.textureScroll = feel.trailTextureScroll;
    trail_.SetStyle(style);

    // 外側の光の帯は覚醒色で、刃先側だけ光って内側へ透ける
    WeaponTrailStyle glow;
    glow.lifetime = lifetime;
    glow.subdivisions = settings_.trailSubdivisions;
    glow.tipColor = { settings_.awakenColor.x, settings_.awakenColor.y, settings_.awakenColor.z, settings_.awakenGlowAlpha };
    glow.baseColor = { settings_.awakenColor.x, settings_.awakenColor.y, settings_.awakenColor.z, 0.0f };
    glowTrail_.SetStyle(glow);
}

/** @brief 刃の軌跡を描画する（3D描画パス内、パーティクルの後に呼ぶ） */
void ComboFeelDirector::DrawTrail() {
    if (!settings_.trailEnabled) {
        return;
    }
    // 光の帯を先に描き、その上に芯の帯を重ねる
    if (IsAwakenBoosted()) {
        glowTrail_.Draw();
    }
    trail_.Draw();
}

// ══════════════════════════════════════════════════════
// 締めの重さ（ブラー・ズーム・スロー）
// ══════════════════════════════════════════════════════

/** @brief 指定位置を中心に締め演出（ブラー・ズーム・スロー）を発動する */
void ComboFeelDirector::TriggerFinisherImpact(const Vector3& worldPos) {
    if (!settings_.impactEnabled) {
        return;
    }
    impactTimer_ = 0.0f;
    impactPos_ = worldPos;
    // 覚醒中の締めはブラー・ズーム・スローをまとめて強める（スローは長くするだけで、遅さは変えない）
    // 武器ごとの重さも掛ける（重い武器ほど強くブレて寄り、長く遅くなる）
    const float awakenScale = IsAwakenBoosted() ? settings_.awakenImpactScale : 1.0f;
    const WeaponFeel& feel = CurrentFeel();
    impactScale_ = awakenScale * feel.impactScale;
    if (settings_.slowEnabled) {
        TimeManager::GetInstance()->RequestSlowMotion(settings_.slowSeconds * awakenScale * feel.slowSecondsScale, settings_.slowScale);
    }
    // 武器によっては、ヒットストップが明けた後も敵だけを止めて、斬り終えた後の間を作る
    if (feel.finisherEnemyStopFrames > 0) {
        TimeManager::GetInstance()->RequestChannelStop(timeChannel::kEnemies, feel.finisherEnemyStopFrames);
    }
}

/** @brief 締め演出の経過時間を進め、ブラーの強さと中心を更新する */
void ComboFeelDirector::UpdateImpact(const Camera* camera) {
    float strength = 0.0f;
    if (impactTimer_ >= 0.0f) {
        // ヒットストップ中こそ見せたい演出なので、ゲームの停止とは無関係に実時間で進める
        impactTimer_ += gameConstants::kFrameDeltaTime;

        const float t = (settings_.blurSeconds > 0.0f) ? impactTimer_ / settings_.blurSeconds : 1.0f;
        if (settings_.impactEnabled && settings_.radialBlurEnabled && t < 1.0f) {
            const float remain = 1.0f - t;
            strength = settings_.blurPeak * impactScale_ * remain * remain;
        }
        // 当たった位置を画面UVへ投影して放射の中心にする（カメラが寄っても中心がずれない）
        if (camera != nullptr) {
            const Matrix4x4 vp = Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
            const Vector3& p = impactPos_;
            const float cx = p.x * vp.m[0][0] + p.y * vp.m[1][0] + p.z * vp.m[2][0] + vp.m[3][0];
            const float cy = p.x * vp.m[0][1] + p.y * vp.m[1][1] + p.z * vp.m[2][1] + vp.m[3][1];
            const float cw = p.x * vp.m[0][3] + p.y * vp.m[1][3] + p.z * vp.m[2][3] + vp.m[3][3];
            if (cw > kMinClipW) {
                // 中心が画面の外（高く打ち上げた敵など）だと、ブラーが画面の外の黒を引き込んで上から黒い枠が出るので、画面の中に収める
                blurCenterU_ = std::clamp(cx / cw * 0.5f + 0.5f, 0.0f, 1.0f);
                blurCenterV_ = std::clamp(0.5f - cy / cw * 0.5f, 0.0f, 1.0f);
            }
        }
        if (impactTimer_ >= (std::max)(settings_.blurSeconds, settings_.zoomSeconds)) {
            impactTimer_ = -1.0f;
        }
    }
    blur_.SetParams(blurCenterU_, blurCenterV_, strength, settings_.blurSamples);
}

/** @brief 締めのラジアルブラーを画面へ掛ける（3D描画の後・UIの前、バックバッファ直描き時だけ呼ぶ） */
void ComboFeelDirector::ApplyScreenEffects() {
    blur_.CaptureAndApply();
}

/** @brief 締めのズームをカメラ位置へ加える（シーンがカメラ位置を決め終えた後に呼ぶ） */
void ComboFeelDirector::ApplyCameraZoom(Camera* camera) const {
    if (camera == nullptr || impactTimer_ < 0.0f || !settings_.impactEnabled || !settings_.zoomEnabled
        || settings_.zoomSeconds <= 0.0f) {
        return;
    }
    const float t = impactTimer_ / settings_.zoomSeconds;
    if (t >= 1.0f) {
        return;
    }
    // 一瞬で寄り、余韻を残しながらゆっくり戻す
    float amount = 0.0f;
    if (t < kZoomAttackRatio) {
        amount = t / kZoomAttackRatio;
    } else {
        const float back = 1.0f - (t - kZoomAttackRatio) / (1.0f - kZoomAttackRatio);
        amount = back * back;
    }
    // カメラは -Z 側から +Z を向いているので、Zを増やすと被写体へ寄る
    const Vector3& cam = camera->GetTranslate();
    camera->SetTranslate({ cam.x, cam.y, cam.z + settings_.zoomDistance * impactScale_ * amount });
}


// ══════════════════════════════════════════════════════
// 振りの向きに沿った斬撃線
// ══════════════════════════════════════════════════════

/** @brief 近接の段が当たった時に、締めなら締め演出を始め、振りの向きの斬撃線を出す */
void ComboFeelDirector::OnMeleeHit(const MeleeAttackDef* attack, bool finisher, const Vector3& hitPos,
    const Vector4& weaponColor, float facingX, const Camera* camera) {
    // 締めの段なら、斬撃線より先にブラー・ズーム・スローの締め演出を始める
    if (finisher) {
        TriggerFinisherImpact(hitPos);
    }
    if (!settings_.slashEnabled || camera == nullptr) {
        return;
    }

    // 振りの向きに沿って、命中点を中心に武器色を白へ寄せた斬撃線を引く（長さ・太さ・残る長さは武器ごとの倍率を掛ける）
    const WeaponFeel& feel = CurrentFeel();
    const Vector2 dir = DecideSlashDirection(attack, facingX);
    const float halfLen = settings_.slashLength * feel.slashLengthScale * (finisher ? settings_.finisherSlashScale : 1.0f);
    const float thickness = (finisher ? settings_.finisherSlashThickness : settings_.slashThickness) * feel.slashThicknessScale;
    const float seconds = settings_.slashSeconds * feel.slashSecondsScale;
    constexpr Vector4 kWhite = { 1.0f, 1.0f, 1.0f, 1.0f };
    const Vector4 color = MixColor(weaponColor, kWhite, kSlashWhiteMix);
    SpawnSlashLine(hitPos, dir, 0.0f, halfLen, thickness, color, seconds, camera);

    // 少しずらした細い線を重ね、一本の線ではなく刃が通った筋に見せる
    const float thinThickness = thickness * kSecondSlashThicknessRatio;
    SpawnSlashLine(hitPos, dir, kSecondSlashOffset, halfLen * kSecondSlashLengthRatio, thinThickness, kWhite, seconds, camera);
    // 武器によっては、さらに平行な細い線を両側へ交互に重ね、何本も刻んだ跡にする（外側ほど短く）
    for (int i = 0; i < feel.slashExtraLines; ++i) {
        const int ring = i / 2 + 1;
        const float side = (i % 2 == 0) ? -1.0f : 1.0f;
        const float lengthRatio = kSecondSlashLengthRatio / static_cast<float>(ring);
        SpawnSlashLine(hitPos, dir, side * feel.slashLineSpread * static_cast<float>(ring), halfLen * lengthRatio, thinThickness, color, seconds, camera);
    }

    // 覚醒中は覚醒色の斬撃線を扇状に重ね、1振りで何本も斬ったように見せる
    if (!IsAwakenBoosted()) {
        return;
    }
    const int extraLines = (std::max)(settings_.awakenSlashCount - 1, 0);
    const float baseAngle = std::atan2(dir.y, dir.x);
    for (int i = 0; i < extraLines; ++i) {
        // 元の線を挟んで左右交互に、外側ほど大きく開く（+1段, -1段, +2段, ...）
        const int stepIndex = i / 2 + 1;
        const float sign = (i % 2 == 0) ? 1.0f : -1.0f;
        const float angle = baseAngle + sign * static_cast<float>(stepIndex) * settings_.awakenSlashSpreadDeg * gameConstants::kDegToRad;
        const Vector2 fan = { std::cos(angle), std::sin(angle) };
        SpawnSlashLine(hitPos, fan, 0.0f, halfLen, thinThickness, settings_.awakenColor, seconds, camera);
    }
}

/** @brief 刃の向きの線を1本引く（中心からの横のずれ・長さ・太さ・色を指定する） */
void ComboFeelDirector::SpawnSlashLine(const Vector3& center, const Vector2& dir, float offset, float halfLen, float thickness,
    const Vector4& color, float seconds, const Camera* camera) const {
    const Vector3& cam = camera->GetTranslate();
    const Vector2 shift = { -dir.y * offset, dir.x * offset };
    sceneShared::SpawnSlashMarkWorld(
        { center.x + shift.x - dir.x * halfLen, center.y + shift.y - dir.y * halfLen },
        { center.x + shift.x + dir.x * halfLen, center.y + shift.y + dir.y * halfLen },
        cam.x, cam.y, color, thickness, seconds);
}

/** @brief 今持っている（最後に振った）武器の手応え */
const WeaponFeel& ComboFeelDirector::CurrentFeel() const {
    return settings_.weaponFeels[static_cast<size_t>(std::clamp(heldWeaponIndex_, 0, ComboFeelSettings::kWeaponTypeCount - 1))];
}

/** @brief 刃先の動きから斬撃線の向きを決める（動きが取れなければ段の振り方から推定する） */
Vector2 ComboFeelDirector::DecideSlashDirection(const MeleeAttackDef* attack, float facingX) const {
    const float facing = (facingX >= 0.0f) ? 1.0f : -1.0f;

    // 実際に刃先が動いた向き（画面は XY 平面なので Z は捨てる）
    Vector3 motion { };
    if (trail_.GetRecentTipMotion(settings_.slashDirectionWindow, motion)) {
        const float len = std::sqrt(motion.x * motion.x + motion.y * motion.y);
        if (len >= kMinSlashMotion) {
            return { motion.x / len, motion.y / len };
        }
    }

    // 刃先の記録が無い時は段の定義から推定する（Z軸の振り=画面内の弧、X軸の振り=縦の振り下ろし/突き上げ）
    Vector2 dir = { facing, 0.0f };
    if (attack != nullptr) {
        const float swingZ = attack->swingTo.z - attack->swingFrom.z;
        const float swingX = attack->swingTo.x - attack->swingFrom.x;
        if (std::abs(swingZ) >= std::abs(swingX)) {
            dir = { facing, (swingZ > 0.0f ? kFallbackDiagonalRise : -kFallbackDiagonalRise) };
        } else {
            dir = { facing * kFallbackVerticalLean, (swingX > 0.0f ? -1.0f : 1.0f) };
        }
    }
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    return { dir.x / len, dir.y / len };
}

// ══════════════════════════════════════════════════════
// 設定の保存・読み込み
// ══════════════════════════════════════════════════════

/** @brief combo_feel.json を読み込み、全項目を読めた場合だけ設定へ反映する */
void ComboFeelDirector::LoadSettings() {
    const nlohmann::json j = JsonHelper::Load(kSettingsPath);
    if (!j.is_object()) {
        return;
    }
    // 手で書き換えた JSON の型違いで落ちないよう、作業用の写しに読み、全部読めた時だけ反映する
    ComboFeelSettings s = settings_;
    try {
        ReadJsonFields(j, kSettingsFields, s);
        if (j.contains("blades") && j["blades"].is_object()) {
            const nlohmann::json& blades = j["blades"];
            for (size_t i = 0; i < kBladeWeaponNames.size(); ++i) {
                if (!blades.contains(kBladeWeaponNames[i])) {
                    continue;
                }
                const nlohmann::json& b = blades[kBladeWeaponNames[i]];
                s.blades[i].base = Vector3FromJson(b.value("base", nlohmann::json { }), s.blades[i].base);
                s.blades[i].tip = Vector3FromJson(b.value("tip", nlohmann::json { }), s.blades[i].tip);
            }
        }
        // 武器ごとの手応えは、書いてある武器の書いてある項目だけを上書きする
        if (j.contains("weaponFeel") && j["weaponFeel"].is_object()) {
            const nlohmann::json& feels = j["weaponFeel"];
            for (size_t i = 0; i < kBladeWeaponNames.size(); ++i) {
                if (feels.contains(kBladeWeaponNames[i])) {
                    ReadJsonFields(feels[kBladeWeaponNames[i]], kWeaponFeelFields, s.weaponFeels[i]);
                    // 手で書いた本数が多すぎても1ヒットの線が増えすぎないよう、パネルと同じ範囲に収める
                    s.weaponFeels[i].slashExtraLines = std::clamp(s.weaponFeels[i].slashExtraLines, 0, kMaxSlashExtraLines);
                }
            }
        }
    } catch (const nlohmann::json::exception&) {
        return;
    }
    settings_ = s;
#ifdef USE_IMGUI
    // 読み込んだ値を、パネルで戻す先の値として覚える
    panelSession_.MarkSaved(settings_);
#endif
}

/** @brief 編集した演出設定をJSONファイルへ保存する */
void ComboFeelDirector::SaveSettings() const {
    const ComboFeelSettings& s = settings_;
    nlohmann::json blades = nlohmann::json::object();
    for (size_t i = 0; i < kBladeWeaponNames.size(); ++i) {
        blades[kBladeWeaponNames[i]] = { { "base", ToJson(s.blades[i].base) }, { "tip", ToJson(s.blades[i].tip) } };
    }
    nlohmann::json j = nlohmann::json::object();
    WriteJsonFields(j, kSettingsFields, s);
    j["blades"] = blades;
    nlohmann::json feels = nlohmann::json::object();
    for (size_t i = 0; i < kBladeWeaponNames.size(); ++i) {
        nlohmann::json feel = nlohmann::json::object();
        WriteJsonFields(feel, kWeaponFeelFields, s.weaponFeels[i]);
        feels[kBladeWeaponNames[i]] = feel;
    }
    j["weaponFeel"] = feels;
    JsonHelper::Save(kSettingsPath, j);
}

} // namespace engine::game
