/**
 * @file KickedBombPool.cpp
 * @brief 攻撃で吹き飛ばした爆弾を飛ばし、ぶつかった所で爆発させる
 */
#include "KickedBombPool.h"
#include "EngineContext.h"
#include "PoolOverflowCounter.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "Object3d.h"
#include <algorithm>
#include <cmath>

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/bomb_kick.json";

/** @brief bomb_kick.json の項目と調整値の対応 */
constexpr JsonField<BombKickTuning> kBombKickFields[] = {
    { nullptr, "enabled", &BombKickTuning::enabled },
    { "visual", "model", &BombKickTuning::model },
    { "visual", "texture", &BombKickTuning::texture },
    { "visual", "spinSpeed", &BombKickTuning::spinSpeed },
    { "flight", "speedX", &BombKickTuning::speedX },
    { "flight", "launchSpeedY", &BombKickTuning::launchSpeedY },
    { "flight", "gravity", &BombKickTuning::gravity },
    { "flight", "maxSeconds", &BombKickTuning::maxSeconds },
    { "hit", "radius", &BombKickTuning::hitRadius },
    { "hit", "launchGraceSeconds", &BombKickTuning::launchGraceSeconds },
    { "effects", "launchImpact", &BombKickTuning::launchImpact },
    { "effects", "trailSparks", &BombKickTuning::trailSparks },
    { "effects", "trailInterval", &BombKickTuning::trailInterval },
    { "wall", "model", &BombKickTuning::wallModel },
    { "wall", "speedX", &BombKickTuning::wallSpeedX },
    { "wall", "launchSpeedY", &BombKickTuning::wallLaunchSpeedY },
    { "wall", "spinSpeed", &BombKickTuning::wallSpinSpeed },
    { "wall", "maxSeconds", &BombKickTuning::wallMaxSeconds },
    { "wall", "enemyDamage", &BombKickTuning::wallEnemyDamage },
    { "wall", "hitRadius", &BombKickTuning::wallHitRadius },
    { "wall", "graceSeconds", &BombKickTuning::wallGraceSeconds },
};

constexpr float kHalf = 0.5f; // 拡縮1の箱の半分の大きさ
constexpr float kStageHalfDepth = 0.5f; // 2.5Dステージの奥行き半分

/** @brief bomb_kick.json から調整値を読み込む */
BombKickTuning LoadTuning() {
    BombKickTuning tuning;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("BombKick: ") + kConfigPath + " が読めません。既定値で動きます");
        return tuning;
    }
    ReadJsonFields(root, kBombKickFields, tuning);
    return tuning;
}
} // namespace

/** @brief 調整値の実体（初回に読み込む） */
BombKickTuning& BombKickTuningStore::Mutable() {
    static BombKickTuning sTuning = LoadTuning();
    return sTuning;
}

/** @brief 読み込み済みの調整値を返す */
const BombKickTuning& BombKickTuningStore::Get() {
    return Mutable();
}

/** @brief bomb_kick.json を読み直す */
void BombKickTuningStore::Reload() {
    Mutable() = LoadTuning();
}

/** @brief 空の入れ物を作る（見た目の実体は Initialize で作る） */
KickedBombPool::KickedBombPool() = default;

/** @brief 見た目の実体を片付ける（Object3d の定義が見えるここで破棄する） */
KickedBombPool::~KickedBombPool() = default;

/** @brief 見た目の実体を全部作る（シーンの初期化で一度だけ呼ぶ） */
void KickedBombPool::Initialize(engine::graphics::ModelCommon* modelCommon) {
    const BombKickTuning& tuning = BombKickTuningStore::Get();
    auto* modelManager = engine::Services().models;
    bombModel_ = modelManager->GetOrLoad(modelCommon, tuning.model, tuning.texture);
    wallModel_ = modelManager->GetOrLoad(modelCommon, tuning.wallModel, tuning.texture);
    // 使い回す数だけ先に作り、ゲーム中は位置と向きとモデルの差し替えだけにする
    for (auto& object : objects_) {
        object = std::make_unique<engine::graphics::Object3d>();
        object->Initialize(modelCommon);
        object->SetModel(bombModel_);
    }
    Clear();
}

/** @brief 爆弾を吹き飛ばす（空きが無ければ false） */
bool KickedBombPool::Launch(const Vector3& position, float dirX, const Vector3& scale, const BombPayload& payload,
    KickedObjectKind kind) {
    const BombKickTuning& tuning = BombKickTuningStore::Get();
    auto freeSlot = std::find_if(bombs_.begin(), bombs_.end(), [](const Bomb& bomb) {
        return !bomb.active;
    });
    if (freeSlot == bombs_.end()) {
        // 空きがない依頼を数える。
        engine::poolOverflowCounter::Count("kicked_bomb_pool");
        return false;
    }
    // 向きだけを残し、速さは種類ごとの調整値で決める
    const bool isWall = kind == KickedObjectKind::kWall;
    const float direction = dirX >= 0.0f ? 1.0f : -1.0f;
    const float speedX = isWall ? tuning.wallSpeedX : tuning.speedX;
    const float speedY = isWall ? tuning.wallLaunchSpeedY : tuning.launchSpeedY;
    Bomb& bomb = *freeSlot;
    bomb.active = true;
    bomb.position = position;
    bomb.velocity = { direction * speedX, speedY, 0.0f };
    bomb.scale = scale;
    bomb.spin = 0.0f;
    bomb.spinDir = -direction;
    bomb.age = 0.0f;
    bomb.trailTimer = 0.0f;
    bomb.payload = payload;
    bomb.kind = kind;
    bomb.sweptTargets.clear();
    // 同じ番号の見た目を、飛ばす物のモデルへ差し替える
    engine::graphics::Object3d* object = objects_[static_cast<size_t>(freeSlot - bombs_.begin())].get();
    if (object != nullptr) {
        object->SetModel(isWall ? wallModel_ : bombModel_);
    }
    return true;
}

/** @brief 飛んでいる壁の当たり判定の箱 */
engine::AABB KickedBombPool::WallBounds(const Bomb& wall) {
    // 回りながら飛ぶので、横幅も高さの半分まで広げて倒れた向きでも敵に届くようにする
    const float halfY = kHalf * std::abs(wall.scale.y);
    const float halfX = (std::max)(kHalf * std::abs(wall.scale.x), halfY);
    return {
        { wall.position.x - halfX, wall.position.y - halfY, -kStageHalfDepth },
        { wall.position.x + halfX, wall.position.y + halfY, kStageHalfDepth }
    };
}

/** @brief 壁・床の箱のどれかに、球がめり込んだか */
bool KickedBombPool::TouchesSolid(const Vector3& position, float radius, const std::vector<engine::AABB>& solids) {
    // 箱の中で球の中心に一番近い点までの距離で調べる（奥行きは2.5Dなので見ない）
    for (const engine::AABB& box : solids) {
        const float nearestX = std::clamp(position.x, box.min.x, box.max.x);
        const float nearestY = std::clamp(position.y, box.min.y, box.max.y);
        const float dx = position.x - nearestX;
        const float dy = position.y - nearestY;
        if (dx * dx + dy * dy <= radius * radius) {
            return true;
        }
    }
    return false;
}

/** @brief 飛んでいる爆弾を進め、ぶつかった物をこのフレームの爆発として返す */
std::vector<BombDetonation> KickedBombPool::Update(float dt, const std::vector<engine::AABB>& solids,
    const EnemyHitTest& hitsEnemy, const WallSweep& sweepEnemies,
    const std::function<void(const Vector3&)>& trailEmitter) {
    const BombKickTuning& tuning = BombKickTuningStore::Get();
    std::vector<BombDetonation> detonations;
    for (size_t i = 0; i < bombs_.size(); ++i) {
        Bomb& bomb = bombs_[i];
        if (!bomb.active) {
            continue;
        }
        const bool isWall = bomb.kind == KickedObjectKind::kWall;
        // 放物線で進め、飛ぶ向きへ転がるように回す
        bomb.age += dt;
        bomb.velocity.y -= tuning.gravity * dt;
        bomb.position = bomb.position + bomb.velocity * dt;
        bomb.spin += bomb.spinDir * (isWall ? tuning.wallSpinSpeed : tuning.spinSpeed) * dt;
        // 飛んでいる間は一定の間隔で火の粉を残す
        bomb.trailTimer -= dt;
        if (bomb.trailTimer <= 0.0f) {
            bomb.trailTimer = tuning.trailInterval;
            trailEmitter(bomb.position);
        }
        // 壁は敵では止まらず、重なった敵を巻き込んで一緒に吹き飛ばす
        bool hitEnemy = false;
        if (isWall) {
            const float dirX = bomb.velocity.x >= 0.0f ? 1.0f : -1.0f;
            sweepEnemies(WallBounds(bomb), dirX, bomb.sweptTargets);
        } else {
            hitEnemy = hitsEnemy(bomb.position, tuning.hitRadius);
        }
        // 爆弾は敵か壁・床、壁は壁・床にぶつかるか、飛びすぎたら砕ける（吹き飛んだ直後は足元の床を数えない）
        const float graceSeconds = isWall ? tuning.wallGraceSeconds : tuning.launchGraceSeconds;
        const float solidRadius = isWall ? tuning.wallHitRadius : tuning.hitRadius;
        const bool hitSolid = bomb.age >= graceSeconds && TouchesSolid(bomb.position, solidRadius, solids);
        const float maxSeconds = isWall ? tuning.wallMaxSeconds : tuning.maxSeconds;
        if (hitEnemy || hitSolid || bomb.age >= maxSeconds) {
            detonations.push_back({ bomb.position, bomb.payload, bomb.kind });
            bomb.active = false;
            continue;
        }
        // 見た目へ今の位置と回転を渡す
        engine::graphics::Object3d* object = objects_[i].get();
        if (object != nullptr) {
            object->SetPosition(bomb.position);
            object->SetRotation({ 0.0f, 0.0f, bomb.spin });
            object->SetScale(bomb.scale);
            object->Update();
        }
    }
    return detonations;
}

/** @brief 飛んでいる爆弾を描く */
void KickedBombPool::Draw() {
    for (size_t i = 0; i < bombs_.size(); ++i) {
        if (bombs_[i].active && objects_[i]) {
            objects_[i]->Draw();
        }
    }
}

/** @brief 飛んでいる爆弾が1つでもあるか */
bool KickedBombPool::HasActive() const {
    return std::any_of(bombs_.begin(), bombs_.end(), [](const Bomb& bomb) {
        return bomb.active;
    });
}

/** @brief 飛んでいる爆弾を全部消す */
void KickedBombPool::Clear() {
    for (Bomb& bomb : bombs_) {
        bomb.active = false;
    }
}

} // namespace engine::game
