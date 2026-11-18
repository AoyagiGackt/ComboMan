/** @file EnemySeparation.cpp
 * @brief 敵の壁接触・敵どうし・プレイヤーとの重なりに基づく位置補正
 */
#include "EnemySeparation.h"
#include "IEnemyEntity.h"
#include "EnemyTuning.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
namespace engine::game {
namespace {
/** @brief 壁と当たる体の大きさ（敵の種類ごとに違う） */
struct WallBodyShape {
    float halfWidth;
    float height;
};

/** @brief 敵の体のうち壁と当たる範囲（足元の床に掛からないよう、位置より少し上から頭まで） */
engine::AABB EnemyWallBody(const Vector3& pos, const WallBodyShape& shape, const EnemySeparationTuning& tuning) {
    return { { pos.x - shape.halfWidth, pos.y + tuning.wallBandBottom, pos.z },
        { pos.x + shape.halfWidth, pos.y + shape.height, pos.z } };
}

/** @brief 体がブロックと横に重なっているか（奥行きは敵の位置の1点で見る） */
bool OverlapsWall(const engine::AABB& body, const engine::AABB& wall) {
    return body.max.y > wall.min.y && body.min.y < wall.max.y
        && body.min.z >= wall.min.z && body.max.z <= wall.max.z
        && body.max.x > wall.min.x && body.min.x < wall.max.x;
}

/** @brief 壁と体のすき間（押し出した直後に計算の誤差で触れたままにならないように） */
constexpr float kWallClearance = 0.001f;

/**
 * @brief 体が横から壁にめり込んでいるか（上下に浅く掛かっているだけの物は横の押し出しの相手にしない）
 * @note 高く跳んで頭が天井に少し入った時に、横一列に並んだ天井の端の外まで押し出さないように
 */
bool OverlapsSideWall(const engine::AABB& body, const engine::AABB& wall) {
    if (!OverlapsWall(body, wall)) {
        return false;
    }
    const float verticalDepth = (std::min)(body.max.y, wall.max.y) - (std::max)(body.min.y, wall.min.y);
    const float horizontalDepth = (std::min)(body.max.x - wall.min.x, wall.max.x - body.min.x);
    return verticalDepth >= horizontalDepth;
}

/** @brief その横位置に置いた体が、どれかの壁に横からめり込んでいるか */
bool OverlapsAnyWall(float x, const Vector3& pos, const WallBodyShape& shape, const std::vector<engine::AABB>& walls, const EnemySeparationTuning& tuning) {
    const engine::AABB body = EnemyWallBody({ x, pos.y, pos.z }, shape, tuning);
    return std::any_of(walls.begin(), walls.end(), [&body](const engine::AABB& wall) {
        return OverlapsSideWall(body, wall);
    });
}

/** @brief その横位置の体の真下に、地形が1つでもあるか（無ければステージの外とみなす） */
bool HasTerrainBelow(float x, const Vector3& pos, const WallBodyShape& shape, const std::vector<engine::AABB>& walls, const EnemySeparationTuning& tuning) {
    const float bodyBottom = pos.y + tuning.wallBandBottom;
    return std::any_of(walls.begin(), walls.end(), [&](const engine::AABB& wall) {
        return wall.max.y <= bodyBottom && x + shape.halfWidth > wall.min.x && x - shape.halfWidth < wall.max.x
            && pos.z >= wall.min.z && pos.z <= wall.max.z;
    });
}

/** @brief 壁にめり込んでいれば、どの壁にもめり込まない一番近い横位置を返す（真下に地形がある所を優先し、ステージの端の壁の外へは出さない） */
float PushOutOfWalls(const Vector3& pos, const WallBodyShape& shape, const std::vector<engine::AABB>& walls, const EnemySeparationTuning& tuning) {
    if (!OverlapsAnyWall(pos.x, pos, shape, walls, tuning)) {
        return pos.x;
    }
    // 並べた壁は1マスずつのブロックなので、1個ずつ浅い側へ押すと隣のブロックへ押し込んで行き来する。
    // 体が掛かる高さの壁の左右の端を候補にして、どの壁にも掛からない一番近い所を選ぶ
    const engine::AABB body = EnemyWallBody(pos, shape, tuning);
    std::optional<float> best;
    std::optional<float> bestOutside; // 真下に何も無い所（ステージの外）。ほかに無い時だけ使う
    for (const engine::AABB& wall : walls) {
        if (body.max.y <= wall.min.y || body.min.y >= wall.max.y || body.min.z < wall.min.z || body.max.z > wall.max.z) {
            continue;
        }
        // 上下に浅く掛かっているだけの物（天井・床）の端は候補にしない
        const float verticalDepth = (std::min)(body.max.y, wall.max.y) - (std::max)(body.min.y, wall.min.y);
        if (verticalDepth < shape.halfWidth) {
            continue;
        }
        const float candidates[] = {
            wall.min.x - shape.halfWidth - kWallClearance,
            wall.max.x + shape.halfWidth + kWallClearance,
        };
        for (float candidate : candidates) {
            if (OverlapsAnyWall(candidate, pos, shape, walls, tuning)) {
                continue;
            }
            std::optional<float>& slot = HasTerrainBelow(candidate, pos, shape, walls, tuning) ? best : bestOutside;
            if (!slot || std::abs(candidate - pos.x) < std::abs(*slot - pos.x)) {
                slot = candidate;
            }
        }
    }
    return best ? *best : bestOutside.value_or(pos.x);
}

/** @brief fromX から toX へ横に動かす途中で新しく当たる壁があれば、その手前で止めた横位置を返す（fromX で既に掛かっている壁は見ない） */
float StopAtFirstWall(float fromX, float toX, const Vector3& pos, const WallBodyShape& shape, const std::vector<engine::AABB>& walls, const EnemySeparationTuning& tuning) {
    if (fromX == toX) {
        return toX;
    }
    const engine::AABB body = EnemyWallBody(pos, shape, tuning);
    const bool movingRight = toX > fromX;
    const float sweepMinX = (std::min)(fromX, toX) - shape.halfWidth;
    const float sweepMaxX = (std::max)(fromX, toX) + shape.halfWidth;
    float stopX = toX;
    for (const engine::AABB& wall : walls) {
        const bool atBodyHeight = body.max.y > wall.min.y && body.min.y < wall.max.y && body.min.z >= wall.min.z && body.max.z <= wall.max.z;
        const bool overlappedBefore = fromX + shape.halfWidth > wall.min.x && fromX - shape.halfWidth < wall.max.x;
        if (!atBodyHeight || overlappedBefore || sweepMaxX <= wall.min.x || sweepMinX >= wall.max.x) {
            continue;
        }
        stopX = movingRight ? (std::min)(stopX, wall.min.x - shape.halfWidth - kWallClearance)
                            : (std::max)(stopX, wall.max.x + shape.halfWidth + kWallClearance);
    }
    return stopX;
}

/** @brief 指定した向き（左=-1, 右=+1）のすぐ横に壁があるか（その向きへは押されても動けない） */
bool WallOnSide(const Vector3& pos, const WallBodyShape& shape, float side, const std::vector<engine::AABB>& walls, const EnemySeparationTuning& tuning) {
    // 体のその向きの端だけを細い帯で調べる（反対側の壁に触れていても誤判定しない）
    const float margin = tuning.wallContactMargin;
    engine::AABB probe = EnemyWallBody(pos, shape, tuning);
    const float edgeX = side < 0.0f ? probe.min.x : probe.max.x;
    probe.min.x = edgeX - margin;
    probe.max.x = edgeX + margin;
    return std::any_of(walls.begin(), walls.end(), [&probe](const engine::AABB& wall) {
        return OverlapsWall(probe, wall);
    });
}
/** @brief 地形全体の左右の端（地形が無ければ空） */
std::optional<std::pair<float, float>> TerrainExtentX(const std::vector<engine::AABB>& walls) {
    if (walls.empty()) {
        return std::nullopt;
    }
    float minX = walls.front().min.x;
    float maxX = walls.front().max.x;
    for (const engine::AABB& wall : walls) {
        minX = (std::min)(minX, wall.min.x);
        maxX = (std::max)(maxX, wall.max.x);
    }
    return std::make_pair(minX, maxX);
}
} // namespace
/** @brief 壁・敵どうし・プレイヤーとの重なりを解消し、見た目を更新した敵を返す */
std::vector<IEnemyEntity*> EnemySeparation::Resolve(const std::vector<Target>& targets, const std::vector<AABB>& walls,
    const std::optional<Vector3>& playerPosition) {
    // 押し出しの強さは 0〜1 に丸める（1で一度に重なりを全部解く）
    const EnemySeparationTuning& tuning = EnemyTuning::GetInstance()->Separation();
    const float pushRate = std::clamp(tuning.pushRate, 0.0f, 1.0f);

    /** @brief 押し離し対象の敵と壁への接触状態 */
    struct Body {
        IEnemyEntity* enemy;
        bool anchored; ///< 押されても動かない（ボス）
        WallBodyShape shape; ///< 壁と当たる体の大きさ
        bool wallLeft = false; ///< 左に壁が接していて左へは動けない
        bool wallRight = false; ///< 右に壁が接していて右へは動けない
        bool moved = false;
        float startX = 0.0f; ///< 押し離す前の横位置（壁から押し出した後なので壁に掛かっていない）
        /** @brief 指定した向き（左=-1, 右=+1）に壁が無く、横へずれられるか（固定かどうかは見ない） */
        bool CanSlide(float side) const {
            return !(side < 0.0f ? wallLeft : wallRight);
        }
        /** @brief 指定した向き（左=-1, 右=+1）へほかの敵に押された時に動けるか */
        bool CanMove(float side) const {
            return !anchored && CanSlide(side);
        }
    };
    // 敵ごとに、動かせるか・左右に壁があるかを持つ
    std::vector<Body> bodies;
    for (const auto& [enemy, anchored] : targets) {
        bodies.push_back({ enemy, anchored, { enemy->WallBodyHalfWidth(tuning.bodyHalfWidth), enemy->WallBodyHeight(tuning.wallBandTop) } });
    }
    std::vector<IEnemyEntity*> moved;
    const std::optional<std::pair<float, float>> extent = TerrainExtentX(walls);
    // 自分の移動で壁にめり込んだ敵を先に押し戻し、壁際の敵は壁の方へは押されないようにする
    constexpr float kLeft = -1.0f;
    constexpr float kRight = 1.0f;
    for (Body& body : bodies) {
        Vector3 pos = body.enemy->GetPosition();
        const float startX = pos.x;
        // 地形全体の端より外（端の壁の向こう）へ出てしまった敵は、どの道で出たかに関係なく、内側へ戻してから押し出す
        if (extent && (pos.x - body.shape.halfWidth < extent->first || pos.x + body.shape.halfWidth > extent->second)) {
            pos.x = std::clamp(pos.x, extent->first + body.shape.halfWidth, (std::max)(extent->first + body.shape.halfWidth, extent->second - body.shape.halfWidth));
        }
        const float resolvedX = PushOutOfWalls(pos, body.shape, walls, tuning);
        body.moved = resolvedX != startX;
        pos.x = resolvedX;
        body.enemy->SetPosition(pos);
        body.startX = pos.x;
        body.wallLeft = WallOnSide(pos, body.shape, kLeft, walls, tuning);
        body.wallRight = WallOnSide(pos, body.shape, kRight, walls, tuning);
    }

    // 近すぎる2体の組ごとに、横へ押し離す
    for (size_t i = 0; i < bodies.size(); ++i) {
        for (size_t j = i + 1; j < bodies.size(); ++j) {
            Body& a = bodies[i];
            Body& b = bodies[j];
            Vector3 posA = a.enemy->GetPosition();
            Vector3 posB = b.enemy->GetPosition();
            const float dx = posB.x - posA.x;
            const float distanceX = std::abs(dx);
            if (distanceX >= tuning.minDistanceX || std::abs(posB.y - posA.y) >= tuning.minDistanceY) {
                continue;
            }
            // 真上に重なっている時は並び順で向きを決め、2体が同じ側へ逃げないようにする
            const float direction = distanceX > 0.0f ? (dx > 0.0f ? 1.0f : -1.0f) : 1.0f;
            const float push = (tuning.minDistanceX - distanceX) * pushRate;
            // A は -direction、B は +direction へずれる。片方が動けない時はもう片方が全部ずれ、両方動ける時は半分ずつ
            const bool canMoveA = a.CanMove(-direction);
            const bool canMoveB = b.CanMove(direction);
            if (!canMoveA && !canMoveB) {
                continue;
            }
            const float shareA = !canMoveB ? 1.0f : (!canMoveA ? 0.0f : 0.5f);
            const float shareB = 1.0f - shareA;
            posA.x -= direction * push * shareA;
            posB.x += direction * push * shareB;
            a.enemy->SetPosition(posA);
            b.enemy->SetPosition(posB);
            a.moved = a.moved || shareA > 0.0f;
            b.moved = b.moved || shareB > 0.0f;
        }
    }

    // プレイヤーに近すぎる敵を、プレイヤーと反対側へ離す（プレイヤーはボスにも押されないよう、ボスも離す）
    // 壁際で離れられない時は、プレイヤー側の押し出し（Player::ResolveEnemyOverlap）に任せる
    if (playerPosition) {
        const float playerPushRate = std::clamp(tuning.playerPushRate, 0.0f, 1.0f);
        for (Body& body : bodies) {
            Vector3 pos = body.enemy->GetPosition();
            const float dx = pos.x - playerPosition->x;
            const float distanceX = std::abs(dx);
            if (distanceX >= tuning.playerMinDistanceX || std::abs(pos.y - playerPosition->y) >= tuning.playerMinDistanceY) {
                continue;
            }
            // 真上に重なっている時は右へ逃がす
            const float direction = dx < 0.0f ? kLeft : kRight;
            if (!body.CanSlide(direction)) {
                continue;
            }
            pos.x += direction * (tuning.playerMinDistanceX - distanceX) * playerPushRate;
            body.enemy->SetPosition(pos);
            body.moved = true;
        }
    }

    // 動いた敵だけを後始末して、呼び出し側へ返す
    for (Body& body : bodies) {
        if (!body.moved) {
            continue;
        }
        // 押し離しで壁へ入った分も戻してから見た目へ反映する。何度も押されて壁の真ん中を越えると、
        // 一番近い外側が壁の向こう側になるので、まず押される前の位置から押された向きへ進めて最初の壁で止める
        Vector3 pos = body.enemy->GetPosition();
        pos.x = StopAtFirstWall(body.startX, pos.x, pos, body.shape, walls, tuning);
        pos.x = PushOutOfWalls(pos, body.shape, walls, tuning);
        body.enemy->SetPosition(pos);
        body.enemy->RefreshVisualTransforms();
        moved.push_back(body.enemy);
    }
    return moved;
}
}
