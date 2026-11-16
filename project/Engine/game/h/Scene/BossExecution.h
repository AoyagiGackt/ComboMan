/**
 * @file BossExecution.h
 * @brief 膝をついたボスへ歩み寄り、突き立てた武器を抜き取って、その武器でとどめを刺す演出を拍ごとに進めるクラス
 * @note 流れは Resources/Config/boss_execution.json の intro（共通の歩み寄り）と styles（ステージごとの型）で決める。
 *       ボスを切り分ける・武器を隠す・武器を渡すのはシーンの部品なので、シーンが Consume～ で受け取って行う
 */
#pragma once
#include "BossExecutionTuning.h"
#include "Vector3.h"
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
class Player;
struct MeleeAttackDef;

/** @brief ボスへのとどめの演出 */
class BossExecution {
public:
    /** @brief 調整値をファイルから読み直す（調整値エディタで保存した時に呼ぶ） */
    static void ReloadTuning();

    /** @brief 演出を始める時にシーンから渡す情報 */
    struct Setup {
        const ExecutionStyle* style = nullptr; ///< 使う型（BossExecutionTuning::FindStyle で選ぶ）
        WeaponType weapon {}; ///< 抜き取るボスの武器（この武器の技の姿勢でとどめを刺す）
        Vector3 bossPosition {}; ///< ボスの足元
    };
    /** @brief 演出が借りるシーンの部品（非所有） */
    struct Services {
        Player* player = nullptr;
        engine::graphics::ParticleManager* particles = nullptr;
        CameraShaker* shaker = nullptr;
        std::mt19937* rng = nullptr;
        const engine::graphics::Camera* camera = nullptr;
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

    /** @brief 演出の間か（間はシーンが操作・敵を止める） */
    bool IsActive() const {
        return active_;
    }
    /** @brief すべての拍と余韻を見せ終えたか（シーンは切り分けの破片が消えるのを待ってから End を呼ぶ） */
    bool IsFinished() const {
        return finished_;
    }
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
    /** @brief 移動を始める（秒数が 0 なら一瞬で移る） */
    void BeginMove(const ExecutionBeat& beat);
    /** @brief 移動を進める */
    void UpdateMove(float dt);
    /** @brief プレイヤーの位置・向き・姿勢を反映する */
    void ApplyPlayerPose();
    /** @brief カメラの見る位置と引きを目標へ寄せる */
    void UpdateCamera();
    /** @brief 姿勢の名前から技の段を引く（無ければ nullptr） */
    const MeleeAttackDef* ResolvePose(const std::string& name) const;
    /** @brief ボスの足元からの位置を世界の位置へ直す（X はプレイヤーが最初にいた側を正にする） */
    Vector3 ToWorld(const Vector3& local) const;
    /** @brief ボスの体の真ん中 */
    Vector3 BossCenter() const;

    Services services_ {};
    std::vector<ExecutionBeat> beats_; ///< 共通の歩み寄りと型の拍をつないだ写し（途中で調整値を読み直しても崩れないように）
    float endHold_ = 0.0f; ///< 最後の拍の後に余韻を見せる秒数
    WeaponType weapon_ {};
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
    const MeleeAttackDef* poseAttack_ = nullptr;
    float poseTimer_ = 0.0f;
    float poseSeconds_ = 0.0f;

    Vector3 cameraFocus_ {};
    float zoom_ = 1.0f;
    float targetZoom_ = 1.0f;
};

} // namespace engine::game
