/**
 * @file GamePlaySceneTypes.h
 * @brief GamePlayScene が中で使う状態の構造体（道中の敵の登録・敵の弾・ボスの衝撃波・自動テストの見張りなど）を定義するファイル
 * @note GamePlayScene の宣言を短く保つため、シーンの外へ出してある。シーンの中では同じ名前の別名で使う
 */
#pragma once
#include "Sprite.h"
#include "Vector3.h"
#include "Weapon.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace engine::game {

class EnemyEntity;
struct MeleeAttackDef;

namespace gamePlayScene {

using engine::graphics::Sprite;

/** @brief 締めの動作で狙う敵1体（ボスかどうかで吹き飛ばしてよいかが変わる） */
struct FinisherTarget {
    EnemyEntity* enemy = nullptr;
    bool isBoss = false;
};

/** @brief 当たり判定ブロック全体を囲むステージの範囲 */
struct StageBounds {
    float left;
    float right;
    float bottom;
    float top;
    bool fromLevel; ///< レベルのブロックから求めたか（false ならブロックが無く既定の広さ）
};

/** @brief 2P が操作に使っている機器（kNone なら味方のボスは自動で戦う） */
enum class AllyDevice {
    kNone,
    kFirstPad, ///< 1台目のパッド（1P がキーボードの時だけ）
    kSecondPad, ///< 2台目のパッド
    kKeyboard, ///< キーボード（1P がパッドの時だけ）
};

/** @brief 倒れて武器を奪える敵1体ぶん（武器の位置と、奪える距離） */
struct StealTarget {
    const EnemyEntity* owner = nullptr;
    Vector3 position;
    float range = 0.0f;
};

/** @brief モンスター1体の個性の動きの途中の状態（GamePlaySceneMonsters.cpp） */
struct MonsterTraitState {
    /** @brief 動きの段階 */
    enum class Phase { kIdle, kTelegraph, kSwoopDown, kSwoopUp };
    Phase phase = Phase::kIdle;
    float timer = 0.0f; ///< 待つ間は次の動きまで、身構える間は残りの秒数
    float pulse = 0.0f; ///< 身構えの輪を次に出すまでの秒数
    float elapsed = 0.0f; ///< 急降下の今の段階の経過秒数
    Vector3 from {}; ///< 急降下を始めた位置（戻る先）
    Vector3 to {}; ///< 急降下で狙う位置
    bool struck = false; ///< この急降下でもう当たったか
};

/** @brief 道中の武器を持った敵1体ぶんの登録内容 */
struct WeaponEnemyEntry {
    /** @brief 登録時の敵の識別番号 */
    uint64_t instanceId = 0;
    EnemyEntity* enemy = nullptr; // 非所有。StageEditorの配置物が実体を所有する
    WeaponType weaponType = WeaponType::kSword;
    bool hasWeapon = true; ///< falseなら武器を持たないモンスター（倒すと消えて覚醒ゲージになる）
    float vanishTimer = 0.0f; ///< モンスターが倒れてから消えるまでの残り秒数
    bool weaponAcquired = false; ///< 武器を奪い終えた（モンスターは消え終えた）
    bool absorbing = false;
    bool defeatEffectEmitted = false;
    float absorbTimer = 0.0f;
    std::unique_ptr<Sprite> hpBarBg; // OnEditorLevelLoaded()で生成（レベル再読込のたびに作り直す）
    std::unique_ptr<Sprite> hpBarFg;
    bool hpBarPlaced = false; ///< HPバーの位置を一度でも決めたか（決める前に描くと画面の右上に白い四角が映る）
    MonsterTraitState trait; ///< モンスターの個性の動きの状態
};

/** @brief 道中の武器敵1体への命中1回ぶんの内訳（何で当てたかと、そこから決まる反応・演出の強さ） */
struct WeaponEnemyHit {
    bool hit = false; ///< 何かが当たった
    bool melee = false; ///< 近接コンボで当てた
    bool gun = false; ///< 銃（スピン連射を含む）で当てた
    bool skill = false; ///< 武器固有技で当てた
    bool fired = false; ///< このフレームに撃った弾で当てた
    bool extraHit = false; ///< 覚醒中の多段ヒットの2発目以降
    bool comboFinisher = false; ///< コンボの締めの段
    bool blowAway = false; ///< 大きく吹き飛ばす
    const MeleeAttackDef* attack = nullptr; ///< 当てた近接の段（無ければ nullptr）
    float fatigueMult = 1.0f; ///< 武器の疲労によるダメージ倍率
};

/** @brief 敵の遠隔攻撃弾1発分の状態 */
struct EnemyBullet {
    Vector3 pos = { };
    Vector3 vel = { };
    float timer = 0.0f;
    int damage = 0;
    float gravity = 0.0f; ///< 下向きの加速度（山なりに飛ぶ骨など。0ならまっすぐ）
};

/** @brief 敵がまとめて撃つ弾の中身（モンスターの火の玉・骨と、ボスの投げで共通） */
struct EnemyVolleyShot {
    int count = 1; ///< 一度に出す数
    float spreadDegrees = 0.0f; ///< 扇状に広げる角度（隣どうしの差）
    float speed = 6.0f; ///< 速さ（山なりの時は横の速さ）
    float riseSpeed = 0.0f; ///< 山なりの時の上向きの速さ
    float gravity = 0.0f; ///< 落ちる加速度（0ならまっすぐ狙う）
    float lifetime = 2.0f; ///< 飛んでいる秒数
    int damage = 1; ///< 当たった時のダメージ
};

/** @brief ボスが地面を叩いて出した、床沿いに走る衝撃波1つ */
struct BossWave {
    float x = 0.0f; ///< 今の横の位置
    float y = 0.0f; ///< 走っている床の高さ（ボスの足元）
    float dirX = 1.0f; ///< 走る向き
    float traveled = 0.0f; ///< 走った距離
    float sparkTimer = 0.0f; ///< 次に土煙を出すまでの秒数
    bool struck = false; ///< もうプレイヤーに当たったか
};

/** @brief 覚醒中の残像トレイル1コマ分の位置と経過秒数（設定された寿命を超えたら消える） */
struct GhostEntry {
    Vector3 pos; ///< 本体の見た目と同じ置き場所（足元合わせ済み）
    float scale; ///< 本体の見た目と同じ倍率
    float yaw; ///< 残した時の向き
    float age;
};

/** @brief 自動テストプレイの見張りが、フレームをまたいで覚えておく様子 */
struct BugWatchState {
    int framesUntilCheck = 0; ///< 次に調べるまでのフレーム数（毎フレームは重いので間を空ける）
    bool wasExpectingProgress = false; ///< 前に調べた時、先へ進む操作をしていたか
    float bestX = 0.0f; ///< 先へ進む操作を始めてから一番右まで行った位置
    float stallSeconds = 0.0f; ///< 一番右の位置が伸びないまま過ぎた秒数
    Vector3 lastGroundedPosition {}; ///< プレイヤーが最後に床に立っていた位置（床抜けの報告に添える）
    Vector3 lastPlayerPosition {}; ///< 前に調べた時のプレイヤーの位置（高い壁や閉じた扉をすり抜けていないか調べる）
    bool hasLastPlayerPosition = false; ///< lastPlayerPosition が入っているか
    uint32_t teleportCount = 0; ///< 前に調べた時の、手順がプレイヤーを移した回数（増えていたら前の位置と比べない）
    std::vector<std::string> playerTrail; ///< 直前のプレイヤーの様子（古い順。床抜けの報告に添える）
    std::vector<std::pair<const EnemyEntity*, uint64_t>> defeatedSeen; ///< 倒されたのを見た敵（生き返りを見つける）
    /** @brief 敵1体の、前に調べた時の様子（理由の分からない大きな移動を見つける） */
    struct EnemyLastSeen {
        Vector3 position {}; ///< 位置
        bool fastMoveExpected = false; ///< 速く動いて当然の状態だったか（ボス・吹き飛び中・台本で動いている）
    };
    std::map<uint64_t, EnemyLastSeen> enemyLastPositions; ///< 敵の番号 → 前に調べた時の様子
    std::vector<std::pair<std::string, float>> suspectSeconds; ///< おかしな様子が続いている秒数（一瞬だけのものは数えない）
};

/** @brief 敵グループの全滅を待つ条件1つの、前のフレームまでの様子（部屋の最後の1体を見つける） */
struct RoomClearWatch {
    bool initialized = false; ///< 一度でも様子を見たか（最初に見た時は演出を出さない）
    bool met = false; ///< 前に見た時に条件が成立していたか
    std::map<std::string, bool> defeated; ///< 敵の名前 → 前に見た時に倒れていたか
    bool hasLastDefeated = false; ///< 最後に倒れた敵を覚えているか
    std::string lastDefeatedName; ///< 最後に倒れた敵の名前
    Vector3 lastDefeatedPosition {}; ///< 最後に倒れた敵が倒れた位置
};

} // namespace gamePlayScene

} // namespace engine::game
