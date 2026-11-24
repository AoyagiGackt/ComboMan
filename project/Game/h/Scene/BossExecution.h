/**
 * @file BossExecution.h
 * @brief 膝をついたボスへ歩み寄り、突き立てた武器を抜き取って、その武器でとどめを刺す演出を拍ごとに進めるクラス
 * @note 流れは Resources/Config/boss_execution.json の intro（共通の歩み寄り）と styles（型）で決める。
 *       近接武器で倒した時は weaponIntro と、倒した武器に割り当てた型を、倒した武器のまま行う（ボスの武器は抜き取らず、崩す時にいっしょに消える）。
 *       銃で倒した時は gunIntro と銃の型を使い、銃で撃ち抜いてから残った武器を抜き取る。
 *       ボスを切り分ける・武器を隠す・武器を渡すのはシーンの部品なので、シーンが Consume～ で受け取って行う
 */
#pragma once
#include "BossExecutionTuning.h"
#include "CollisionConfig.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Weapon.h"
#include <random>
#include <string>
#include <vector>

namespace engine::graphics {
class Camera;
class ParticleManager;
}

namespace engine::game {

class CameraShaker;
class ComboFeelDirector;
class Player;
struct GunShotDef;
struct MeleeAttackDef;
struct MeleeComboSet;

/** @brief ボスへのとどめの演出 */
class BossExecution {
public:
    /** @brief 調整値をファイルから読み直す（調整値エディタで保存した時に呼ぶ） */
    static void ReloadTuning();

    /** @brief 演出を始める時にシーンから渡す情報 */
    struct Setup {
        const ExecutionStyle* style = nullptr; ///< 使う型（BossExecutionTuning::FindStyle で選ぶ）
        WeaponType weapon {}; ///< 抜き取るボスの武器（この武器の技の姿勢でとどめを刺す）
        bool byGun = false; ///< 銃で倒したか（歩み寄りを gunIntro にする。style には銃の型を渡す）
        bool byBomb = false; ///< 爆弾がめり込んだまま倒したか（歩み寄りを bombIntro にする。style には爆弾の型を渡す）
        bool byOwnWeapon = false; ///< 倒した近接武器のままとどめを刺すか（歩み寄りを weaponIntro にする。ボスの武器は抜き取らない）
        WeaponType ownWeapon {}; ///< 倒した近接武器（byOwnWeapon の時、抜き取るまではこの武器の技の姿勢）
        GunType gun {}; ///< 持っている銃（射撃の段の姿勢と弾数に使う）
        Vector4 gunColor = { 1.0f, 1.0f, 1.0f, 1.0f }; ///< 弾の色
        Vector3 bossPosition {}; ///< ボスの足元
        float standingY = 0.0f; ///< プレイヤーが床に立った時の高さ（宙で始めても、真下の床に立たせた高さ）
        const std::vector<engine::AABB>* terrain = nullptr; ///< 地形（ボスの向こう側へ回り込む広さを調べる。無ければ調べない）
    };
    /** @brief 演出が借りるシーンの部品（非所有） */
    struct Services {
        Player* player = nullptr;
        engine::graphics::ParticleManager* particles = nullptr;
        CameraShaker* shaker = nullptr;
        std::mt19937* rng = nullptr;
        const engine::graphics::Camera* camera = nullptr;
        const ComboFeelDirector* comboFeel = nullptr; ///< 刃の根元と先端の位置を引く（武器を床へめり込ませないため。無ければ起こさない）
    };

    /** @brief 演出を最初から始める（プレイヤーは今いる側からボスへ歩み寄る） */
    void Start(const Setup& setup, const Services& services);
    /**
     * @brief 1フレーム進める
     * @param dt ゲームの経過秒（ヒットストップ中は 0、スロー中は縮む）
     */
    void Update(float dt);
    /** @brief 演出を閉じ、プレイヤーを技の姿勢から戻す（シーンが武器を渡した後に呼ぶ） */
    void End();

    /** @brief 演出で使うボスの足元（壁際で倒した時は、回り込む広さを空けるため倒した所から手前へずらしてある） */
    const Vector3& GetBossPosition() const {
        return bossPos_;
    }
    /** @brief 演出の間か（間はシーンが操作・敵を止める） */
    bool IsActive() const {
        return active_;
    }
    /** @brief すべての拍と余韻を見せ終えたか（シーンは切り分けの破片が消えるのを待ってから End を呼ぶ） */
    bool IsFinished() const {
        return finished_;
    }
    /** @brief 銃で撃ち抜く流れか（シーンは崩す時に刃を放たず、突き立った武器を残す） */
    bool IsByGun() const {
        return byGun_;
    }
    /** @brief 爆弾がめり込んだまま倒した隠しの流れか（シーンは崩す時に刃を放たず、突き立った武器を残す） */
    bool IsByBomb() const {
        return byBomb_;
    }
    /** @brief 倒した近接武器のままとどめを刺す流れか（シーンはボスの武器を手に入れても持ち替えさせない） */
    bool IsByOwnWeapon() const {
        return byOwnWeapon_;
    }
    /** @brief めり込んだ爆弾を爆発させる拍の最初の1回だけ true（シーンは爆弾を壊して爆発を出す） */
    bool ConsumeBomb();
    /** @brief 武器を抜き取った最初の1回だけ true（シーンはボスの武器を隠す） */
    bool ConsumeGrab();
    /** @brief ボスを切り分ける拍の最初の1回だけ true（シーンはボスを破片に差し替える） */
    bool ConsumeSlice();
    /** @brief カメラが追う位置（演出中はプレイヤーの代わりにこれを追う） */
    const Vector3& CameraFocus() const {
        return cameraFocus_;
    }
    /** @brief カメラの引き（1 が通常、小さいほど寄る） */
    float CameraZoom() const {
        return zoom_;
    }

private:
    /** @brief 次に行う拍（共通の歩み寄りの後に型の拍が続く。もう無ければ nullptr） */
    const ExecutionBeat* NextBeat() const;
    /** @brief 拍に書いてある事をまとめて行う */
    void Fire(const ExecutionBeat& beat);
    /** @brief 拍の効果（火花・輪・閃光・音・止め・揺れ・スロー）を出す */
    void FireEffects(const ExecutionBeat& beat);
    /** @brief 拍の斬撃線を出す */
    void FireSlash(const ExecutionBeat& beat);
    /** @brief 拍に書いてある姿勢を取り始める（射撃の段は、撃たない拍なら構え切るまで、撃つ拍なら発砲の瞬間から） */
    void StartPose(const ExecutionBeat& beat);
    /** @brief プレイヤーからボスの体の真ん中へ弾を飛ばす */
    void FireShot();
    /** @brief 今の姿勢の経過割合（0〜1。射撃の段は構えと発砲で動かす範囲が違う） */
    float PoseProgress() const;
    /** @brief 移動を始める（秒数が 0 なら一瞬で移る） */
    void BeginMove(const ExecutionBeat& beat);
    /** @brief 移動を進める */
    void UpdateMove(float dt);
    /** @brief プレイヤーの位置・向き・姿勢を反映する */
    void ApplyPlayerPose();
    /** @brief プレイヤーの位置・向き・姿勢を反映する（ApplyPlayerPose の下請け。武器の床の補正は掛けない） */
    void PosePlayer(float facing);
    /** @brief 手に持つ武器の刃が床より下へ行くなら、握りを中心に起こして床の上に収める */
    void KeepWeaponAboveFloor(float facing);
    /** @brief カメラの見る位置と引きを目標へ寄せる */
    void UpdateCamera();
    /** @brief 今の技の姿勢に使う武器（倒した武器のままの流れなら抜き取るまで倒した武器、それ以外は抜き取るボスの武器） */
    WeaponType PoseWeapon() const;
    /** @brief プレイヤーが床から浮いているか（跳びのいている最中など。宙にいる姿にする） */
    bool IsAirborne() const;
    /** @brief 今持っている武器のまま技の姿勢を取るか（倒した武器が持っている武器で、まだボスの武器を抜き取っていない時。合体の見た目もそのまま） */
    bool UsesEquippedWeapon() const;
    /** @brief 今の技の姿勢に使うコンボ一式（持っているボスの枠の武器のままなら、その専用の技） */
    const MeleeComboSet& PoseComboSet() const;
    /** @brief 姿勢の名前から技の段を引く（無ければ nullptr） */
    const MeleeAttackDef* ResolvePose(const std::string& name) const;
    /** @brief 姿勢の名前から射撃の段を引く（射撃の名前でなければ nullptr） */
    const GunShotDef* ResolveShot(const std::string& name) const;
    /** @brief ボスの足元からの位置を世界の位置へ直す（X はプレイヤーが最初にいた側を正にする） */
    Vector3 ToWorld(const Vector3& local) const;
    /** @brief ボスの体の真ん中 */
    Vector3 BossCenter() const;
    /** @brief 拍の動く先が壁に掛かるなら、ボスとプレイヤーをそろって動かす量を返す（正でプレイヤーが最初にいた側へ） */
    float RoomAroundBossShift(const std::vector<engine::AABB>& terrain) const;

    Services services_ {};
    std::vector<ExecutionBeat> beats_; ///< 共通の歩み寄りと型の拍をつないだ写し（途中で調整値を読み直しても崩れないように）
    float endHold_ = 0.0f; ///< 最後の拍の後に余韻を見せる秒数
    WeaponType weapon_ {};
    bool byGun_ = false; ///< 銃で撃ち抜く流れか
    bool byBomb_ = false; ///< めり込んだ爆弾で吹き飛ばす流れか
    bool byOwnWeapon_ = false; ///< 倒した近接武器のままとどめを刺す流れか
    WeaponType ownWeapon_ {}; ///< 倒した近接武器
    GunType gun_ {}; ///< 持っている銃
    Vector4 gunColor_ = { 1.0f, 1.0f, 1.0f, 1.0f }; ///< 弾の色
    Vector3 bossPos_ {};
    float side_ = 1.0f; ///< プレイヤーが最初にいた側（+1 で +X 側）
    float groundY_ = 0.0f; ///< プレイヤーが立つ床の高さ
    float playerZ_ = 0.0f;

    bool active_ = false;
    bool finished_ = false;
    size_t beatIndex_ = 0; ///< beats_ の次に行う拍の番号
    float beatTimer_ = 0.0f; ///< 前の拍からの経過秒
    float endTimer_ = 0.0f; ///< 最後の拍からの経過秒

    Vector3 playerPos_ {};
    Vector3 moveFrom_ {};
    Vector3 moveTo_ {};
    float moveTimer_ = 0.0f;
    float moveSeconds_ = 0.0f;
    bool moving_ = false;
    bool walking_ = false; ///< 歩く動きで移っているか
    bool faceAway_ = false;

    bool grabbed_ = false;
    bool grabPending_ = false;
    bool slicePending_ = false;
    bool bombPending_ = false; ///< 爆弾を爆発させる拍を行って、シーンがまだ受け取っていないか
    const MeleeAttackDef* poseAttack_ = nullptr;
    const GunShotDef* poseShot_ = nullptr; ///< 取っている射撃の段（近接の姿勢か素の姿なら nullptr）
    bool gunPosed_ = false; ///< プレイヤーに射撃の姿勢を取らせているか（離れる時に構えを解く）
    float poseTimer_ = 0.0f;
    float poseSeconds_ = 0.0f;
    float poseFrom_ = 0.0f; ///< 姿勢を動かし始める経過割合
    float poseTo_ = 1.0f; ///< 姿勢を動かし終える経過割合

    Vector3 cameraFocus_ {};
    float zoom_ = 1.0f;
    float targetZoom_ = 1.0f;
};

} // namespace engine::game
