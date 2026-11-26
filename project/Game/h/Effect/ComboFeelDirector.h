/**
 * @file ComboFeelDirector.h
 * @brief 近接コンボの手応え演出（刃の軌跡・締めの重さ・振りの向きに沿った斬撃線）をまとめて管理するファイル
 */
#pragma once
#include "ImpactRadialBlurEffect.h"
#include "MakeAffine.h"
#include "Weapon.h"
#include "WeaponTrailEffect.h"
#include <array>
#include <string>
#ifdef USE_IMGUI
#include "PanelEditSession.h"
#endif

namespace engine {
class DirectXCommon;
class Input;
} // namespace engine
namespace engine::graphics {
class Camera;
} // namespace engine::graphics

namespace engine::game {

class Player;
struct MeleeAttackDef;

/** @brief 武器モデル空間での刃の根元と先端（軌跡の帯の両端になる） */
struct BladeRange {
    Vector3 base;
    Vector3 tip;
};

/**
 * @brief 武器ごとの手応えの違い（共通の調整値に掛ける倍率）
 * @note 軽い武器は短く細く刻み、重い武器は太く長く残して締めを重くする、といった性格を付ける
 */
struct WeaponFeel {
    float trailLifetimeScale = 1.0f; ///< 刃の軌跡が残る長さの倍率
    float trailAlphaScale = 1.0f; ///< 刃の軌跡の濃さの倍率
    float slashLengthScale = 1.0f; ///< 斬撃線の長さの倍率
    float slashThicknessScale = 1.0f; ///< 斬撃線の太さの倍率
    float slashSecondsScale = 1.0f; ///< 斬撃線が残る長さの倍率
    int slashExtraLines = 0; ///< 斬撃線に平行に重ねる細い線の本数（刻んだ跡を何本も残す）
    float slashLineSpread = 0.0f; ///< 平行に重ねる線どうしの間（ワールド単位）
    float impactScale = 1.0f; ///< 締めのブラーと寄りの強さの倍率
    float slowSecondsScale = 1.0f; ///< 締めのスローの長さの倍率
    std::string trailTexture; ///< 刃の軌跡に掛ける筋の画像（空なら一様な帯）
    float trailIntensity = 1.0f; ///< 刃の軌跡の明るさの倍率（1を超えると芯が白く飛び、ブルームで光る）
    float trailTextureRepeat = 1.0f; ///< 軌跡の長さに筋の画像を何回並べるか
    float trailTextureScroll = 0.0f; ///< 筋の画像を軌跡に沿って流す速さ（1秒に画像の何枚ぶん）
    int finisherEnemyStopFrames = 0; ///< 締めの後、全体のヒットストップが明けてから敵だけを止めるフレーム数（自分は動ける）
};

/** @brief 演出ごとのオン/オフと調整値（Resources/Config/combo_feel.json に保存する） */
struct ComboFeelSettings {
    static constexpr int kWeaponTypeCount = static_cast<int>(engine::game::kWeaponTypeCount); ///< WeaponType の要素数
    static constexpr const char* kFilePath = "Resources/Config/combo_feel.json"; ///< 保存先
    static constexpr int kMaxSlashExtraLines = 6; ///< 斬撃線に平行に重ねる細い線の上限
    /** @brief WeaponType の並び順の武器の名前（パネルと JSON のキーに使う） */
    static constexpr std::array<const char*, kWeaponTypeCount> kBladeWeaponNames = {
        "Sword", "Spear", "Hammer", "Dagger", "Ball", "Greatsword", "Scythe", "Axe"
    };

    // 刃の軌跡
    bool trailEnabled = true;
    int trailSubdivisions = 6; ///< サンプル間の補間分割数（1で補間なし）
    float trailLifetime = 0.16f; ///< 帯が消えるまでの秒数
    float trailTipAlpha = 0.85f; ///< 刃先側の不透明度
    float trailBaseAlpha = 0.0f; ///< 根元側の不透明度
    float trailTint = 0.35f; ///< 刃先の色を武器の属性色へ寄せる割合（0で白）
    std::array<BladeRange, kWeaponTypeCount> blades { }; ///< WeaponType 順の刃の範囲

    // 締めの重さ
    bool impactEnabled = true;
    bool radialBlurEnabled = true;
    float blurPeak = 0.22f; ///< ラジアルブラーの最大強度
    float blurSeconds = 0.22f; ///< ブラーが抜けきるまでの秒数
    int blurSamples = 16; ///< ブラーのサンプル数
    bool zoomEnabled = true;
    float zoomDistance = 2.2f; ///< カメラを寄せる最大距離
    float zoomSeconds = 0.28f; ///< 寄って戻るまでの秒数
    bool slowEnabled = true;
    float slowScale = 0.35f; ///< ヒットストップ明けのスローでゲームを進める割合
    float slowSeconds = 0.22f; ///< スローを続ける秒数

    // 振りの向きに沿った斬撃線
    bool slashEnabled = true;
    float slashLength = 1.6f; ///< 斬撃線の半分の長さ（ワールド単位）
    float slashThickness = 6.0f; ///< 通常段の太さ（ピクセル）
    float finisherSlashThickness = 13.0f; ///< 締めの太さ（ピクセル）
    float finisherSlashScale = 1.6f; ///< 締めの長さ倍率
    float slashSeconds = 0.2f; ///< 表示時間
    float slashDirectionWindow = 0.06f; ///< 刃先の動きを何秒さかのぼって向きを決めるか

    // 覚醒中の上乗せ
    bool awakenEnabled = true;
    Vector4 awakenColor = { 0.35f, 0.8f, 1.0f, 1.0f }; ///< 覚醒中の軌跡・斬撃線の色
    float awakenTint = 0.7f; ///< 軌跡の刃先を覚醒色へ寄せる割合
    float awakenTrailLifetimeScale = 1.6f; ///< 軌跡の寿命の倍率（長く残す）
    float awakenGlowExtend = 0.35f; ///< 外側の光の帯を刃先からさらに伸ばす割合（刃の長さに対する比）
    float awakenGlowAlpha = 0.45f; ///< 外側の光の帯の不透明度
    int awakenSlashCount = 3; ///< 1ヒットで出す斬撃線の本数
    float awakenSlashSpreadDeg = 16.0f; ///< 斬撃線を扇状に広げる角度（度）
    float awakenImpactScale = 1.4f; ///< 締め演出（ブラー・ズーム・スロー）の強さの倍率

    // 武器ごとの手応え
    std::array<WeaponFeel, kWeaponTypeCount> weaponFeels { }; ///< WeaponType 順の武器ごとの倍率

    ComboFeelSettings();
};

/**
 * @brief 近接コンボの手応え演出の司令塔
 * @note シーンからは Update → OnMeleeHit → ApplyCameraZoom → DrawTrail の順で呼ぶ。
 *       演出は全部ここで完結し、シーン側は当たった事実と位置を渡すだけにする。
 *       F6 のデバッグパネルで各演出を個別にオン/オフでき、比較と調整がその場でできる。
 */
class ComboFeelDirector {
public:
    void Initialize(engine::DirectXCommon* dxCommon, engine::graphics::SrvManager* srvManager);

    /** @brief 演出の状態を消す（シーン終了時に呼ぶ） */
    void Finalize();

    /**
     * @brief 刃の位置を積み、締め演出の時間を進める（毎フレーム呼ぶ）
     * @param player       刃の位置と攻撃中の段を読むプレイヤー
     * @param weaponColor  装備中の武器の属性色
     * @param camera       ブラー中心の投影と軌跡の描画に使うカメラ
     * @param gameAdvanced このフレームにゲームが進んだか（ヒットストップ・スローで間引かれた時はfalse）
     */
    void Update(const Player& player, const Vector4& weaponColor, engine::graphics::Camera* camera, bool gameAdvanced);

    /**
     * @brief 近接攻撃が当たった時に呼ぶ
     * @param attack      当たった段（nullptrなら向きは向いている方向の水平になる）
     * @param finisher    締めの段か（締め演出を発動する）
     * @param hitPos      当たった位置（ワールド座標）
     * @param weaponColor 武器の属性色
     * @param facingX     プレイヤーの向き（+1/-1）
     * @param camera      斬撃線をスクリーンへ投影するカメラ
     */
    void OnMeleeHit(const MeleeAttackDef* attack, bool finisher, const Vector3& hitPos,
        const Vector4& weaponColor, float facingX, const engine::graphics::Camera* camera);

    /** @brief 締め演出（ブラー・ズーム・スロー）を指定位置を中心に発動する */
    void TriggerFinisherImpact(const Vector3& worldPos);

    /** @brief 覚醒中かを伝える（軌跡の二重化・斬撃線の本数・締め演出の強さが変わる。毎フレーム呼ぶ） */
    void SetAwakened(bool awakened) {
        awakened_ = awakened;
    }

    /** @brief 締めのズームをカメラ位置へ加える（シーンがカメラ位置を決め終えた後に呼ぶ） */
    void ApplyCameraZoom(engine::graphics::Camera* camera) const;

    /** @brief 刃の軌跡を描画する（3D描画パス内、パーティクルの後に呼ぶ） */
    void DrawTrail();

    /**
     * @brief 締めのラジアルブラーを画面へ掛ける（3D描画の後・UIの前、バックバッファ直描き時だけ呼ぶ）
     * @note 描画先とPSOを書き換えるので、呼び出し側でメイン描画先の復帰を行うこと
     */
    void ApplyScreenEffects();

    /** @brief ラジアルブラーを描く必要があるか（描画先の復帰を省くための判定） */
    bool HasScreenEffects() const {
        return blur_.IsActive();
    }

    /**
     * @brief 手に持っている武器の刃の根元と先端をワールド座標で返す
     * @return 武器を手に持っていない（投げている・素手）なら false
     */
    bool GetBladeWorld(const Player& player, Vector3& outBase, Vector3& outTip) const;

    /** @brief F6でデバッグパネルを開閉し、開いていれば描く（Updateフェーズから呼ぶこと） */
    void UpdateDebugPanel(engine::Input* input);


private:
    /** @brief 刃の範囲を武器行列でワールドへ移して軌跡に積む */
    void SampleBlade(const Player& player);
    /** @brief 締め演出の経過時間を進め、ブラーの強さと中心を更新する */
    void UpdateImpact(const engine::graphics::Camera* camera);
    /** @brief 軌跡の色を設定値と武器の色から作る */
    void ApplyTrailStyle(const Vector4& weaponColor);


    /** @brief 刃先の動きから斬撃線の向きを決める（動きが取れなければ段の振り方から推定する） */
    Vector2 DecideSlashDirection(const MeleeAttackDef* attack, float facingX) const;

    void LoadSettings();
    void SaveSettings() const;

    /** @brief 今持っている（最後に振った）武器の手応え */
    const WeaponFeel& CurrentFeel() const;
    /** @brief 刃の向きの線を1本引く（中心からの横のずれ・長さ・太さ・色を指定する） */
    void SpawnSlashLine(const Vector3& center, const Vector2& dir, float offset, float halfLen, float thickness,
        const Vector4& color, float seconds, const engine::graphics::Camera* camera) const;

    /** @brief 覚醒の上乗せを今使うか */
    bool IsAwakenBoosted() const {
        return (awakened_ || previewAwaken_) && settings_.awakenEnabled;
    }

    ComboFeelSettings settings_;
    engine::graphics::WeaponTrailEffect trail_;
    engine::graphics::WeaponTrailEffect glowTrail_; ///< 覚醒中に刃の外側へ重ねる光の帯
    bool awakened_ = false;
    bool previewAwaken_ = false; ///< 覚醒していなくても覚醒中の見た目にする（F6パネルでの確認用）
    float impactScale_ = 1.0f; ///< 発動中の締め演出の強さの倍率（発動した瞬間の覚醒状態で決める）

    const MeleeAttackDef* lastAttack_ = nullptr; ///< 段が変わったら軌跡を切るための前フレームの段
    int lastWeaponIndex_ = 0; ///< デバッグパネルで刃の範囲を編集する対象
    int heldWeaponIndex_ = 0; ///< 最後に振った武器（武器ごとの手応えを選ぶのに使う。パネルの選択では変わらない）
    Vector3 lastPlayerPos_ = { }; ///< デバッグパネルから締め演出を試す時の中心
    float lastTrailMicros_ = 0.0f; ///< 軌跡の更新にかかったCPU時間（マイクロ秒）

    float impactTimer_ = -1.0f; ///< 締め演出の経過秒数（負なら停止中）
    Vector3 impactPos_ = { };

    engine::graphics::ImpactRadialBlurEffect blur_;
    float blurCenterU_ = 0.5f; ///< ブラー中心（画面UV）
    float blurCenterV_ = 0.5f;

    bool showPanel_ = false;
#ifdef USE_IMGUI
    engine::editor::PanelEditSession<ComboFeelSettings> panelSession_; ///< パネルの元に戻す・戻す操作・説明
#endif
};

} // namespace engine::game
