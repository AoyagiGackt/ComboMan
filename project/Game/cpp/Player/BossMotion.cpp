/**
 * @file BossMotion.cpp
 * @brief ステージボスの技ごとの動きの読み込み（boss_motion.json）と再生
 */
#include "BossMotion.h"
#include "GameConstants.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/boss_motion.json";
constexpr const char* kDefaultBoss = "default"; ///< どのボスにも無い技の動きを探す名前
constexpr size_t kReachBoneIndex = 0; ///< reaches の1件 [骨, 向き, 割合] の骨の位置
constexpr size_t kReachDirectionIndex = 1;
constexpr size_t kReachWeightIndex = 2;
constexpr size_t kReachItemCount = 3;
constexpr size_t kVectorSize = 3;
constexpr float kSmoothA = 3.0f; ///< なめらかにつなぐ曲線 t*t*(3-2t) の係数
constexpr float kSmoothB = 2.0f;

/** @brief 0〜1 の割合を、始まりと終わりがゆっくりの曲線へ直す */
float Smooth(float t) {
    const float c = std::clamp(t, 0.0f, 1.0f);
    return c * c * (kSmoothA - kSmoothB * c);
}

/** @brief 角度を -π〜π の内へ戻す（一周ぶんの違いを無くす） */
float WrapAngle(float angle) {
    return std::remainder(angle, gameConstants::kTwoPi);
}

/** @brief 数値を読む（無ければ既定値） */
float ReadFloat(const nlohmann::json& object, const char* key, float fallback) {
    const auto found = object.find(key);
    return found != object.end() && found->is_number() ? found->get<float>() : fallback;
}

/** @brief [x, y, z] を読む（形が違えば既定値） */
Vector3 ReadVector(const nlohmann::json& value, const Vector3& fallback) {
    if (!value.is_array() || value.size() < kVectorSize) {
        return fallback;
    }
    // 数字でない要素があれば読まない（そのまま取り出すと例外で止まる）
    for (size_t i = 0; i < kVectorSize; ++i) {
        if (!value[i].is_number()) {
            return fallback;
        }
    }
    return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
}

/** @brief キー1つを読む（角度は度で書き、ラジアンにして持つ） */
BossMotionKey ReadKey(const nlohmann::json& item) {
    BossMotionKey key;
    key.time = ReadFloat(item, "t", 0.0f);
    key.pose.lean = ReadFloat(item, "lean", 0.0f) * gameConstants::kDegToRad;
    key.pose.lift = ReadFloat(item, "lift", 0.0f);
    key.pose.spin = ReadFloat(item, "spin", 0.0f) * gameConstants::kDegToRad;
    key.pose.weaponAngle = ReadFloat(item, "weapon", 0.0f) * gameConstants::kDegToRad;
    const auto offset = item.find("weaponOffset");
    if (offset != item.end()) {
        key.pose.weaponOffset = ReadVector(*offset, {});
    }
    // [骨, [x, y, z], 割合] の並び
    const auto reaches = item.find("reaches");
    if (reaches == item.end() || !reaches->is_array()) {
        return key;
    }
    for (const nlohmann::json& reach : *reaches) {
        if (!reach.is_array() || reach.size() < kReachItemCount || !reach[kReachBoneIndex].is_string()) {
            continue;
        }
        BossMotionReach entry;
        entry.bone = reach[kReachBoneIndex].get<std::string>();
        entry.direction = ReadVector(reach[kReachDirectionIndex], entry.direction);
        entry.weight = reach[kReachWeightIndex].is_number() ? reach[kReachWeightIndex].get<float>() : 0.0f;
        key.pose.reaches.push_back(entry);
    }
    return key;
}

/** @brief 区切り1つを読む */
BossMotionClip ReadClip(const nlohmann::json& object) {
    BossMotionClip clip;
    if (!object.is_object()) {
        return clip;
    }
    const auto anim = object.find("anim");
    if (anim != object.end() && anim->is_string()) {
        clip.anim = anim->get<std::string>();
    }
    clip.animSpeed = ReadFloat(object, "animSpeed", clip.animSpeed);
    clip.loopSeconds = ReadFloat(object, "loopSeconds", clip.loopSeconds);
    const auto keys = object.find("keys");
    if (keys != object.end() && keys->is_array()) {
        for (const nlohmann::json& item : *keys) {
            clip.keys.push_back(ReadKey(item));
        }
    }
    std::sort(clip.keys.begin(), clip.keys.end(), [](const BossMotionKey& a, const BossMotionKey& b) {
        return a.time < b.time;
    });
    return clip;
}

/** @brief 技1つを読む */
BossMoveMotion ReadMove(const nlohmann::json& object) {
    BossMoveMotion move;
    if (!object.is_object()) {
        return move;
    }
    const auto windup = object.find("windup");
    if (windup != object.end()) {
        move.windup = ReadClip(*windup);
    }
    const auto action = object.find("action");
    if (action != object.end()) {
        move.action = ReadClip(*action);
    }
    const auto recover = object.find("recover");
    if (recover != object.end()) {
        move.recover = ReadClip(*recover);
    }
    return move;
}

/** @brief 区切りの名前（区切りが変わったかを見分ける） */
const char* StageName(BossMotionStage stage) {
    switch (stage) {
    case BossMotionStage::kWindup:
        return "windup";
    case BossMotionStage::kRecover:
        return "recover";
    default:
        return "action";
    }
}
} // namespace

/** @brief 2つの姿勢の間 */
BossPose LerpBossPose(const BossPose& a, const BossPose& b, float t) {
    BossPose result;
    result.lean = a.lean + (b.lean - a.lean) * t;
    result.lift = a.lift + (b.lift - a.lift) * t;
    result.spin = a.spin + (b.spin - a.spin) * t;
    result.weaponAngle = a.weaponAngle + (b.weaponAngle - a.weaponAngle) * t;
    result.weaponOffset = Lerp(a.weaponOffset, b.weaponOffset, t);
    // a の骨を順に並べ、b にしか無い骨を後ろへ足す（親から子の順はどちらのキーでも同じに書く）
    for (const BossMotionReach& from : a.reaches) {
        const auto to = std::find_if(b.reaches.begin(), b.reaches.end(), [&](const BossMotionReach& r) {
            return r.bone == from.bone;
        });
        BossMotionReach mixed = from;
        if (to != b.reaches.end()) {
            mixed.direction = Lerp(from.direction, to->direction, t);
            mixed.weight = from.weight + (to->weight - from.weight) * t;
        } else {
            mixed.weight = from.weight * (1.0f - t);
        }
        result.reaches.push_back(mixed);
    }
    for (const BossMotionReach& to : b.reaches) {
        const bool inA = std::any_of(a.reaches.begin(), a.reaches.end(), [&](const BossMotionReach& r) {
            return r.bone == to.bone;
        });
        if (!inA) {
            BossMotionReach mixed = to;
            mixed.weight = to.weight * t;
            result.reaches.push_back(mixed);
        }
    }
    return result;
}

/** @brief 指定した経過割合の姿勢 */
BossPose BossMotionClip::Evaluate(float time) const {
    if (keys.empty()) {
        return {};
    }
    if (time <= keys.front().time) {
        return keys.front().pose;
    }
    if (time >= keys.back().time) {
        return keys.back().pose;
    }
    // time を挟む2つのキーの間をなめらかにつなぐ
    for (size_t i = 1; i < keys.size(); ++i) {
        const BossMotionKey& next = keys[i];
        if (time > next.time) {
            continue;
        }
        const BossMotionKey& prev = keys[i - 1];
        const float span = next.time - prev.time;
        const float local = span > 0.0f ? (time - prev.time) / span : 1.0f;
        return LerpBossPose(prev.pose, next.pose, Smooth(local));
    }
    return keys.back().pose;
}

/** @brief 区切りの動きを返す */
const BossMotionClip& BossMoveMotion::Clip(BossMotionStage stage) const {
    switch (stage) {
    case BossMotionStage::kWindup:
        return windup;
    case BossMotionStage::kRecover:
        return recover;
    default:
        return action;
    }
}

/** @brief 書き換えられる実体 */
BossMotionLibrary& BossMotionLibrary::Mutable() {
    static BossMotionLibrary sLibrary = Load();
    return sLibrary;
}

/** @brief 読み込み済みの中身を返す */
const BossMotionLibrary& BossMotionLibrary::Get() {
    return Mutable();
}

/** @brief boss_motion.json を読み直す */
void BossMotionLibrary::Reload() {
    Mutable() = Load();
}

/** @brief JSON から読む */
BossMotionLibrary BossMotionLibrary::Load() {
    BossMotionLibrary library;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("BossMotion: ") + kConfigPath + " が読めません。ボスは技ごとの動きをしません");
        return library;
    }
    library.blendSeconds_ = ReadFloat(root, "blendSeconds", library.blendSeconds_);
    const auto hand = root.find("handBone");
    if (hand != root.end() && hand->is_string()) {
        library.handBone_ = hand->get<std::string>();
    }
    const auto onHand = root.find("weaponOnHand");
    if (onHand != root.end() && onHand->is_boolean()) {
        library.weaponOnHand_ = onHand->get<bool>();
    }
    const auto bosses = root.find("bosses");
    if (bosses == root.end() || !bosses->is_object()) {
        return library;
    }
    for (auto boss = bosses->begin(); boss != bosses->end(); ++boss) {
        if (!boss.value().is_object()) {
            continue;
        }
        for (auto move = boss.value().begin(); move != boss.value().end(); ++move) {
            library.motions_[boss.key()][move.key()] = ReadMove(move.value());
        }
    }
    return library;
}

/** @brief ボスと技の動き */
const BossMoveMotion& BossMotionLibrary::Find(const std::string& boss, const std::string& move) const {
    static const BossMoveMotion kEmpty {};
    for (const std::string& name : { boss, std::string(kDefaultBoss) }) {
        const auto bossFound = motions_.find(name);
        if (bossFound == motions_.end()) {
            continue;
        }
        const auto moveFound = bossFound->second.find(move);
        if (moveFound != bossFound->second.end()) {
            return moveFound->second;
        }
    }
    return kEmpty;
}

/** @brief 構えの最初へ戻す */
void BossMotionPlayer::Reset() {
    current_ = {};
    blendFrom_ = {};
    lastKey_.clear();
    blendTimer_ = 0.0f;
    loopTime_ = 0.0f;
}

/** @brief 1フレーム進めて、体へ掛ける動きを返す */
BossMotionFrame BossMotionPlayer::Update(const std::string& boss, const BossMotionCue& cue, float dt) {
    const BossMotionLibrary& library = BossMotionLibrary::Get();
    const BossMotionClip& clip = library.Find(boss, cue.move).Clip(cue.stage);
    // 技か区切りが変わったら、直前の姿勢からつなぎ始める
    const std::string key = cue.move + "/" + StageName(cue.stage);
    if (key != lastKey_) {
        lastKey_ = key;
        blendFrom_ = current_;
        blendTimer_ = library.BlendSeconds();
        loopTime_ = 0.0f;
    }
    loopTime_ += dt;
    // 繰り返す動きは自分の時計で、それ以外は進行役の経過割合で進める
    const float time = clip.loopSeconds > 0.0f ? std::fmod(loopTime_, clip.loopSeconds) / clip.loopSeconds : cue.progress;
    const BossPose target = clip.Evaluate(time);
    if (blendTimer_ > 0.0f && library.BlendSeconds() > 0.0f) {
        blendTimer_ = (std::max)(blendTimer_ - dt, 0.0f);
        // 宙返りや回転で一周以上回った角度は、逆回りや余計な一回転をしないよう、行き先に近い向きへ読み替えてからつなぐ
        BossPose from = blendFrom_;
        from.lean = target.lean + WrapAngle(from.lean - target.lean);
        from.spin = target.spin + WrapAngle(from.spin - target.spin);
        from.weaponAngle = target.weaponAngle + WrapAngle(from.weaponAngle - target.weaponAngle);
        current_ = LerpBossPose(from, target, Smooth(1.0f - blendTimer_ / library.BlendSeconds()));
    } else {
        current_ = target;
    }
    BossMotionFrame frame;
    frame.pose = current_;
    frame.anim = clip.anim;
    frame.animSpeed = clip.animSpeed;
    frame.handBone = library.HandBone();
    frame.weaponOnHand = library.WeaponOnHand();
    return frame;
}

} // namespace engine::game
