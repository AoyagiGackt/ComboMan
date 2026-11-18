/**
 * @file BossMotion.h
 * @brief ステージボスの技ごとの動き（体の傾き・腕の向き・武器の角度のキー）と、その再生役を定義するファイル
 * @note 中身は Resources/Config/boss_motion.json。ボス（武器種別名）ごと、技ごとに、身構え・動いている間・隙の3つの区切りでキーを持つ
 */
#pragma once
#include "Vector3.h"
#include <map>
#include <string>
#include <vector>

namespace engine::game {

/** @brief 1本の骨をモデルの座標のどの向きへ伸ばすか（+Z が体の前、+Y が上） */
struct BossMotionReach {
    std::string bone; ///< 骨の名前
    Vector3 direction = { 0.0f, 1.0f, 0.0f }; ///< 伸ばす向き（長さは問わない）
    float weight = 0.0f; ///< 寄せる割合（0で元のアニメのまま、1でこの向き）
};

/** @brief ある瞬間のボスの姿勢 */
struct BossPose {
    float lean = 0.0f; ///< 体を前へ傾ける角度（ラジアン、負なら反らす）
    float lift = 0.0f; ///< 見た目だけ持ち上げる高さ（負なら沈める）
    float spin = 0.0f; ///< 縦の軸まわりに体を回す角度（ラジアン）
    float weaponAngle = 0.0f; ///< 武器の角度（ラジアン、0で刃先が真上、正で前へ倒す、負で背中側へ倒す）
    Vector3 weaponOffset = {}; ///< 手元からの武器のずれ（x=前 y=上 z=奥）
    std::vector<BossMotionReach> reaches; ///< 骨ごとの向き
};

/** @brief 姿勢のキー1つ */
struct BossMotionKey {
    float time = 0.0f; ///< 区切りの中での経過割合（0〜1）
    BossPose pose; ///< その時の姿勢
};

/** @brief 技の1区切りぶんの動き */
struct BossMotionClip {
    std::string anim; ///< 下に流すアニメの名前（空なら今のアニメのまま）
    float animSpeed = 1.0f; ///< 下に流すアニメの速さ
    float loopSeconds = 0.0f; ///< 繰り返す長さ（0なら経過割合で進める。構えの息づかいなどに使う）
    std::vector<BossMotionKey> keys; ///< 時刻順のキー

    /** @brief キーがあるか */
    bool IsEmpty() const {
        return keys.empty();
    }
    /** @brief 指定した経過割合の姿勢（キーの間はなめらかにつなぐ） */
    BossPose Evaluate(float time) const;
};

/** @brief 技の区切り */
enum class BossMotionStage {
    kWindup, ///< 身構え
    kAction, ///< 動いている間（突進・ダッシュ・跳んでいる間。構え・咆哮・ひるみもここ）
    kRecover, ///< 技の後の隙（投げ・衝撃波・叩きつけは身構えの次がすぐここなので、最初のキーが打ち込んだ瞬間の姿勢）
};

/** @brief 技1つぶんの動き */
struct BossMoveMotion {
    BossMotionClip windup; ///< 身構え
    BossMotionClip action; ///< 動いている間
    BossMotionClip recover; ///< 隙

    /** @brief 区切りの動きを返す */
    const BossMotionClip& Clip(BossMotionStage stage) const;
};

/** @brief 今ボスが何をしているか（ボス戦の進行役が毎フレーム出す） */
struct BossMotionCue {
    std::string move = "stance"; ///< 技の名前（stance=構え roar=咆哮 stagger=ひるみ ほかは boss_battle.json の技の名前）
    BossMotionStage stage = BossMotionStage::kAction; ///< 区切り
    float progress = 0.0f; ///< 区切りの中での経過割合（0〜1）
};

/** @brief 1フレームぶんの、ボスの体へ掛ける動き（EnemyEntity が受け取る） */
struct BossMotionFrame {
    BossPose pose; ///< 姿勢
    std::string anim; ///< 下に流すアニメの名前（空なら変えない）
    float animSpeed = 1.0f; ///< 下に流すアニメの速さ
    std::string handBone = "Palm.R"; ///< 武器を持たせる手の骨
    bool weaponOnHand = true; ///< 武器を手の骨の位置へ付けるか（false なら体からの決まった位置）
};

/** @brief ボスの動きの置き場（初回に読み、Reload で読み直す） */
class BossMotionLibrary {
public:
    /** @brief 読み込み済みの中身を返す */
    static const BossMotionLibrary& Get();
    /** @brief boss_motion.json を読み直す */
    static void Reload();

    /** @brief ボスと技の動き（無ければ default の同じ技、それも無ければ空） */
    const BossMoveMotion& Find(const std::string& boss, const std::string& move) const;
    /** @brief 区切りが変わった時に前の姿勢からつなぐ秒数 */
    float BlendSeconds() const {
        return blendSeconds_;
    }
    /** @brief 武器を持たせる手の骨 */
    const std::string& HandBone() const {
        return handBone_;
    }
    /** @brief 武器を手の骨の位置へ付けるか */
    bool WeaponOnHand() const {
        return weaponOnHand_;
    }

private:
    /** @brief 書き換えられる実体 */
    static BossMotionLibrary& Mutable();
    /** @brief JSON から読む */
    static BossMotionLibrary Load();

    std::map<std::string, std::map<std::string, BossMoveMotion>> motions_; ///< [ボス][技]
    float blendSeconds_ = 0.12f;
    std::string handBone_ = "Palm.R";
    bool weaponOnHand_ = true;
};

/**
 * @brief ボスの動きの再生役
 * @note 区切りや技が変わった時は、直前に見せていた姿勢から BlendSeconds かけてつなぐ（姿勢が飛ばないように）
 */
class BossMotionPlayer {
public:
    /** @brief 構えの最初へ戻す（ボスを設定し直した時に呼ぶ） */
    void Reset();
    /**
     * @brief 1フレーム進めて、体へ掛ける動きを返す
     * @param boss ボスの名前（武器種別名）
     * @param cue  今ボスが何をしているか
     * @param dt   経過秒数
     */
    BossMotionFrame Update(const std::string& boss, const BossMotionCue& cue, float dt);

private:
    BossPose current_; ///< 直前に見せた姿勢
    BossPose blendFrom_; ///< つなぎ始めた時の姿勢
    std::string lastKey_; ///< 直前の技と区切り（変わったらつなぎ始める）
    float blendTimer_ = 0.0f; ///< つなぎの残り秒数
    float loopTime_ = 0.0f; ///< 繰り返す動きの経過秒数
};

/** @brief 2つの姿勢の間（t=0でa、1でb。片方にしか無い骨は、無い側を寄せない姿勢として扱う） */
BossPose LerpBossPose(const BossPose& a, const BossPose& b, float t);

} // namespace engine::game
