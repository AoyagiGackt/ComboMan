/**
 * @file KickedBombPool.cpp
 * @brief 攻撃で吹き飛ばした爆弾を飛ばし、ぶつかった所で爆発させる
 */
#include "KickedBombPool.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "Object3d.h"
#include <algorithm>

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
};

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
    engine::graphics::Model* model = engine::graphics::ModelManager::GetInstance()->GetOrLoad(modelCommon, tuning.model, tuning.texture);
    // 使い回す数だけ先に作り、ゲーム中は位置と向きを変えるだけにする
    for (auto& object : objects_) {
        object = std::make_unique<engine::graphics::Object3d>();
        object->Initialize(modelCommon);
        object->SetModel(model);
    }
    Clear();
}

/** @brief 爆弾を吹き飛ばす（空きが無ければ false） */
bool KickedBombPool::Launch(const Vector3& position, float dirX, const Vector3& scale, const BombPayload& payload) {
    const BombKickTuning& tuning = BombKickTuningStore::Get();
    auto freeSlot = std::find_if(bombs_.begin(), bombs_.end(), [](const Bomb& bomb) {
        return !bomb.active;
    });
    if (freeSlot == bombs_.end()) {
        return false;
    }
    // 向きだけを残し、速さは調整値で決める
    const float direction = dirX >= 0.0f ? 1.0f : -1.0f;
    Bomb& bomb = *freeSlot;
    bomb.active = true;
    bomb.position = position;
    bomb.velocity = { direction * tuning.speedX, tuning.launchSpeedY, 0.0f };
    bomb.scale = scale;
    bomb.spin = 0.0f;
    bomb.spinDir = -direction;
    bomb.age = 0.0f;
    bomb.trailTimer = 0.0f;
    bomb.payload = payload;
    return true;
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
    const EnemyHitTest& hitsEnemy, const std::function<void(const Vector3&)>& trailEmitter) {
    const BombKickTuning& tuning = BombKickTuningStore::Get();
    std::vector<BombDetonation> detonations;
    for (size_t i = 0; i < bombs_.size(); ++i) {
        Bomb& bomb = bombs_[i];
        if (!bomb.active) {
            continue;
        }
        // 放物線で進め、飛ぶ向きへ転がるように回す
        bomb.age += dt;
        bomb.velocity.y -= tuning.gravity * dt;
        bomb.position = bomb.position + bomb.velocity * dt;
        bomb.spin += bomb.spinDir * tuning.spinSpeed * dt;
        // 飛んでいる間は一定の間隔で火の粉を残す
        bomb.trailTimer -= dt;
        if (bomb.trailTimer <= 0.0f) {
            bomb.trailTimer = tuning.trailInterval;
            trailEmitter(bomb.position);
        }
        // 敵か壁・床にぶつかるか、飛びすぎたら爆発する（吹き飛んだ直後は足元の床を数えない）
        const bool hitEnemy = hitsEnemy(bomb.position, tuning.hitRadius);
        const bool hitSolid = bomb.age >= tuning.launchGraceSeconds && TouchesSolid(bomb.position, tuning.hitRadius, solids);
        if (hitEnemy || hitSolid || bomb.age >= tuning.maxSeconds) {
            detonations.push_back({ bomb.position, bomb.payload });
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
