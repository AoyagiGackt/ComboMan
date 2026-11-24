/**
 * @file BossBattleDirector.cpp
 * @brief ステージボス戦の進行（ひるみ値・第2段階・武器ごとの技）
 */
#include "BossBattleDirector.h"
#include "EnemyEntity.h"
#include "EnemyTuning.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/boss_battle.json";
constexpr const char* kSlamAttack = "slam";
constexpr const char* kChargeAttack = "charge";
constexpr const char* kDashAttack = "dash";
constexpr const char* kWaveAttack = "wave";
constexpr const char* kLeapAttack = "leap";
constexpr const char* kVortexAttack = "vortex";
constexpr const char* kStanceMotion = "stance"; // 技を出していない間の動きの名前
constexpr const char* kRoarMotion = "roar"; // 咆哮の動きの名前
constexpr const char* kStaggerMotion = "stagger"; // ひるみの動きの名前
constexpr float kLeapArcScale = 4.0f; // 0〜1 の放物線 t(1-t) の頂点を 1 にする倍率

/** @brief 投げる技1種類ぶんの項目と調整値の対応 */
constexpr JsonField<BossVolleyData> kVolleyFields[] = {
    { nullptr, "telegraphSeconds", &BossVolleyData::telegraphSeconds },
    { nullptr, "count", &BossVolleyData::count },
    { nullptr, "spreadDegrees", &BossVolleyData::spreadDegrees },
    { nullptr, "speed", &BossVolleyData::speed },
    { nullptr, "riseSpeed", &BossVolleyData::riseSpeed },
    { nullptr, "gravity", &BossVolleyData::gravity },
    { nullptr, "lifetime", &BossVolleyData::lifetime },
    { nullptr, "launchHeight", &BossVolleyData::launchHeight },
    { nullptr, "damage", &BossVolleyData::damage },
    { nullptr, "recoverSeconds", &BossVolleyData::recoverSeconds },
    { nullptr, "warningRing", &BossVolleyData::warningRing },
};

/** @brief boss_battle.json の項目と調整値の対応 */
constexpr JsonField<BossBattleTuning> kBossFields[] = {
    { "engage", "range", &BossBattleTuning::engageRange },
    { "stagger", "hpRatio", &BossBattleTuning::staggerHpRatio },
    { "stagger", "rageHpRatio", &BossBattleTuning::rageStaggerHpRatio },
    { "stagger", "seconds", &BossBattleTuning::staggerSeconds },
    { "stagger", "impact", &BossBattleTuning::staggerImpact },
    { "stagger", "ring", &BossBattleTuning::staggerRing },
    { "rage", "hpRatio", &BossBattleTuning::rageHpRatio },
    { "rage", "seconds", &BossBattleTuning::rageSeconds },
    { "rage", "color", &BossBattleTuning::rageColor },
    { "rage", "intervalMult", &BossBattleTuning::rageIntervalMult },
    { "rage", "radiusMult", &BossBattleTuning::rageRadiusMult },
    { "rage", "impact", &BossBattleTuning::rageImpact },
    { "rage", "flash", &BossBattleTuning::rageFlash },
    { "rage", "ring", &BossBattleTuning::rageRing },
    { "attack", "firstDelay", &BossBattleTuning::firstAttackDelay },
    { "attack", "interval", &BossBattleTuning::attackInterval },
    { "slam", "recoverSeconds", &BossBattleTuning::slamRecoverSeconds },
    { "charge", "telegraphSeconds", &BossBattleTuning::chargeTelegraphSeconds },
    { "charge", "speed", &BossBattleTuning::chargeSpeed },
    { "charge", "distance", &BossBattleTuning::chargeDistance },
    { "charge", "recoverSeconds", &BossBattleTuning::chargeRecoverSeconds },
    { "charge", "damage", &BossBattleTuning::chargeDamage },
    { "charge", "warningRing", &BossBattleTuning::chargeWarningRing },
    { "charge", "trailSparks", &BossBattleTuning::chargeTrailSparks },
    { "dash", "count", &BossBattleTuning::dashCount },
    { "dash", "telegraphSeconds", &BossBattleTuning::dashTelegraphSeconds },
    { "dash", "seconds", &BossBattleTuning::dashSeconds },
    { "dash", "offset", &BossBattleTuning::dashOffset },
    { "dash", "recoverSeconds", &BossBattleTuning::dashRecoverSeconds },
    { "dash", "damage", &BossBattleTuning::dashDamage },
    { "dash", "warningRing", &BossBattleTuning::dashWarningRing },
    { "dash", "trailSparks", &BossBattleTuning::dashTrailSparks },
    { "wave", "telegraphSeconds", &BossBattleTuning::waveTelegraphSeconds },
    { "wave", "speed", &BossBattleTuning::waveSpeed },
    { "wave", "distance", &BossBattleTuning::waveDistance },
    { "wave", "height", &BossBattleTuning::waveHeight },
    { "wave", "halfWidth", &BossBattleTuning::waveHalfWidth },
    { "wave", "damage", &BossBattleTuning::waveDamage },
    { "wave", "recoverSeconds", &BossBattleTuning::waveRecoverSeconds },
    { "wave", "warningRing", &BossBattleTuning::waveWarningRing },
    { "wave", "sparks", &BossBattleTuning::waveSparks },
    { "leap", "telegraphSeconds", &BossBattleTuning::leapTelegraphSeconds },
    { "leap", "seconds", &BossBattleTuning::leapSeconds },
    { "leap", "height", &BossBattleTuning::leapHeight },
    { "leap", "radius", &BossBattleTuning::leapRadius },
    { "leap", "damage", &BossBattleTuning::leapDamage },
    { "leap", "recoverSeconds", &BossBattleTuning::leapRecoverSeconds },
    { "leap", "warningRing", &BossBattleTuning::leapWarningRing },
    { "vortex", "telegraphSeconds", &BossBattleTuning::vortexTelegraphSeconds },
    { "vortex", "pullSeconds", &BossBattleTuning::vortexPullSeconds },
    { "vortex", "pullRadius", &BossBattleTuning::vortexPullRadius },
    { "vortex", "pullSpeed", &BossBattleTuning::vortexPullSpeed },
    { "vortex", "height", &BossBattleTuning::vortexHeight },
    { "vortex", "burstRadius", &BossBattleTuning::vortexBurstRadius },
    { "vortex", "damage", &BossBattleTuning::vortexDamage },
    { "vortex", "recoverSeconds", &BossBattleTuning::vortexRecoverSeconds },
    { "vortex", "warningRing", &BossBattleTuning::vortexWarningRing },
    { "vortex", "debris", &BossBattleTuning::vortexDebris },
};

/** @brief JSON の文字列の並びを読む（無ければ既定のまま） */
void ReadStringList(const nlohmann::json& object, const char* key, std::vector<std::string>& out) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        return;
    }
    std::vector<std::string> values;
    for (const nlohmann::json& item : *it) {
        if (item.is_string()) {
            values.push_back(item.get<std::string>());
        }
    }
    if (!values.empty()) {
        out = std::move(values);
    }
}

/** @brief boss_battle.json から調整値とボスの一覧を読み込む */
BossBattleTuning LoadTuning() {
    BossBattleTuning tuning;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("BossBattle: ") + kConfigPath + " が読めません。既定値で動きます");
        return tuning;
    }
    ReadJsonFields(root, kBossFields, tuning);
    // 投げる技は名前ごとに中身を読む
    const auto volleys = root.find("volleys");
    if (volleys != root.end() && volleys->is_object()) {
        for (auto it = volleys->begin(); it != volleys->end(); ++it) {
            if (it->is_object()) {
                tuning.volleys[it.key()] = ReadJsonFields(*it, kVolleyFields);
            }
        }
    }
    // ボスは武器種別名ごとに、名前・一言・技の並びを読む
    const auto bosses = root.find("bosses");
    if (bosses != root.end() && bosses->is_object()) {
        for (auto it = bosses->begin(); it != bosses->end(); ++it) {
            if (!it->is_object()) {
                continue;
            }
            BossProfile profile;
            profile.name = it->value("name", profile.name);
            ReadStringList(*it, "attacks", profile.attacks);
            ReadStringList(*it, "rageAttacks", profile.rageAttacks);
            tuning.profiles[it.key()] = std::move(profile);
        }
    }
    return tuning;
}

/** @brief 減速しながら近づく補間（0〜1） */
float EaseOutQuad(float t) {
    return 1.0f - (1.0f - t) * (1.0f - t);
}
} // namespace

/** @brief 調整値の実体（初回に読み込む） */
BossBattleTuning& BossBattleTuningStore::Mutable() {
    static BossBattleTuning sTuning = LoadTuning();
    return sTuning;
}

/** @brief 読み込み済みの調整値を返す */
const BossBattleTuning& BossBattleTuningStore::Get() {
    return Mutable();
}

/** @brief boss_battle.json を読み直す */
void BossBattleTuningStore::Reload() {
    Mutable() = LoadTuning();
}

/** @brief 戦うボスを設定し、戦う前の状態に戻す */
void BossBattleDirector::Bind(EnemyEntity* boss, const std::string& weaponName) {
    // 前のボスの実体はステージの読み直しで消えているので触らない（台本モードは実体ごと無くなる）
    boss_ = boss;
    bossKey_ = weaponName;
    currentAttack_.clear();
    stageSeconds_ = 0.0f;
    const BossBattleTuning& tuning = BossBattleTuningStore::Get();
    const auto found = tuning.profiles.find(weaponName);
    profile_ = found != tuning.profiles.end() ? found->second : BossProfile {};
    phase_ = Phase::kWaiting;
    move_ = Move::kNone;
    raging_ = false;
    phaseTimer_ = 0.0f;
    attackTimer_ = tuning.firstAttackDelay;
    moveTimer_ = 0.0f;
    attackIndex_ = 0;
    strikeLanded_ = false;
    volley_ = {};
    staggered_ = false;
    staggerTimer_ = 0.0f;
    poise_ = 0.0f;
    lastHp_ = boss_ != nullptr ? boss_->GetHp() : 0;
    // 戦いが始まるまでは持ち場で待つ
    if (boss_ != nullptr) {
        boss_->SetChaseUnleashed(false);
    }
}

/** @brief 叩きつけの範囲の倍率（第2段階で広がる） */
float BossBattleDirector::SlamRadiusMult() const {
    return raging_ ? BossBattleTuningStore::Get().rageRadiusMult : 1.0f;
}

/** @brief 台本モードを切り替え、向きをプレイヤーへ合わせる */
void BossBattleDirector::SetScripted(bool scripted, const Vector3& playerPosition) {
    boss_->SetScripted(scripted);
    if (scripted) {
        boss_->SetScriptedFacing(playerPosition.x - boss_->GetPosition().x);
    }
}

/** @brief 1フレーム進める */
void BossBattleDirector::Update(float dt, const BossFrameInput& input, BossFrameOutput& output) {
    if (boss_ == nullptr) {
        return;
    }
    // 倒れたら何もしない（倒れた敵は台本モードとして扱われない）
    if (boss_->IsDefeated()) {
        move_ = Move::kNone;
        return;
    }
    const BossBattleTuning& tuning = BossBattleTuningStore::Get();
    // 体力が減った分は、どの段階でもひるみ値にためる
    const int hp = boss_->GetHp();
    if (hp < lastHp_) {
        poise_ += static_cast<float>(lastHp_ - hp);
    }
    lastHp_ = hp;
    switch (phase_) {
    case Phase::kWaiting:
        UpdateWaiting(input);
        break;
    case Phase::kRage:
        // 咆哮の間は止まり、終わったら戦闘へ戻る
        phaseTimer_ -= dt;
        if (phaseTimer_ <= 0.0f) {
            phase_ = Phase::kFight;
            SetScripted(false, input.playerPosition);
            attackTimer_ = tuning.firstAttackDelay;
        }
        break;
    case Phase::kFight:
        UpdateFight(dt, input, output);
        break;
    }
    output.motion = CurrentMotionCue();
}

/** @brief 戦う前: プレイヤーが近づいたら戦い始める（壁や閉じた扉の向こうにいる間はその場で待つ） */
void BossBattleDirector::UpdateWaiting(const BossFrameInput& input) {
    const BossBattleTuning& tuning = BossBattleTuningStore::Get();
    // 扉が閉じている間は扉の方へ歩いて来ないよう止めておく
    SetScripted(!input.canSeePlayer, input.playerPosition);
    if (!input.canSeePlayer || !boss_->IsVisible() || DistanceSquaredXY(input.playerPosition, boss_->GetPosition()) > tuning.engageRange * tuning.engageRange) {
        return;
    }
    // 近づく前に当てた分は数えず、戦い始めからためる
    phase_ = Phase::kFight;
    // 戦い始めたら持ち場に縛らずに追う（少し離れた所が安全地帯にならないように）
    boss_->SetChaseUnleashed(true);
    attackTimer_ = tuning.firstAttackDelay;
    poise_ = 0.0f;
}

/** @brief 受けたダメージをひるみ値にため、たまったらひるませる（ひるんでいる間は true を返す） */
bool BossBattleDirector::UpdateStagger(float dt, const BossFrameInput& input, BossFrameOutput& output) {
    const BossBattleTuning& tuning = BossBattleTuningStore::Get();
    // ひるんでいる間は技を出さず、時間が来たら元の硬さへ戻す
    if (staggered_) {
        staggerTimer_ -= dt;
        if (staggerTimer_ <= 0.0f) {
            staggered_ = false;
            poise_ = 0.0f;
            attackTimer_ = tuning.firstAttackDelay;
        }
        return true;
    }
    const float ratio = raging_ ? tuning.rageStaggerHpRatio : tuning.staggerHpRatio;
    const float threshold = static_cast<float>(boss_->GetMaxHp()) * ratio;
    if (threshold <= 0.0f || poise_ < threshold) {
        return false;
    }
    // 技の途中でも打ち切って、吹き飛ばせる状態にする（跳びかかりで宙にいたら、ステージボスは高さが変わらないので踏み切った高さへ戻す）
    if (move_ == Move::kLeaping) {
        Vector3 grounded = boss_->GetPosition();
        grounded.y = leapFrom_.y;
        boss_->SetPosition(grounded);
    }
    staggered_ = true;
    staggerTimer_ = tuning.staggerSeconds;
    poise_ = 0.0f;
    move_ = Move::kNone;
    SetScripted(false, input.playerPosition);
    output.staggerStarted = true;
    return true;
}

/** @brief 戦闘中: ひるみ値と体力を見て、ひるむか咆哮へ移るか、技を進める */
void BossBattleDirector::UpdateFight(float dt, const BossFrameInput& input, BossFrameOutput& output) {
    const BossBattleTuning& tuning = BossBattleTuningStore::Get();
    if (UpdateStagger(dt, input, output)) {
        return;
    }
    // 技の最中でなければ、体力が減った所で咆哮して第2段階へ
    const float hpRatio = boss_->GetMaxHp() > 0
        ? static_cast<float>(boss_->GetHp()) / static_cast<float>(boss_->GetMaxHp())
        : 1.0f;
    if (!raging_ && move_ == Move::kNone && hpRatio <= tuning.rageHpRatio) {
        raging_ = true;
        phase_ = Phase::kRage;
        phaseTimer_ = tuning.rageSeconds;
        attackIndex_ = 0;
        SetScripted(true, input.playerPosition);
        output.rageStarted = true;
        return;
    }
    if (move_ == Move::kNone) {
        // 戦いの途中でも、壁の向こうへ離れられたら追わずに待ち、技も出さない
        SetScripted(!input.canSeePlayer, input.playerPosition);
        if (!input.canSeePlayer) {
            return;
        }
        attackTimer_ -= dt;
        if (attackTimer_ <= 0.0f) {
            StartNextAttack(input, output);
        }
        return;
    }
    UpdateMove(dt, input, output);
}

/** @brief 技の間隔を数え、次の技を選んで始める */
void BossBattleDirector::StartNextAttack(const BossFrameInput& input, BossFrameOutput& output) {
    const BossBattleTuning& tuning = BossBattleTuningStore::Get();
    const std::vector<std::string>& attacks = raging_ ? profile_.rageAttacks : profile_.attacks;
    attackTimer_ = tuning.attackInterval * (raging_ ? tuning.rageIntervalMult : 1.0f);
    if (attacks.empty()) {
        return;
    }
    // 技の一覧を順に回す
    const std::string& attack = attacks[attackIndex_ % attacks.size()];
    ++attackIndex_;
    strikeLanded_ = false;
    currentAttack_ = attack;
    if (attack == kSlamAttack) {
        // 予告の円と着弾はシーンが出し、ボスはその間その場で振りかぶる
        output.slamRequested = true;
        BeginTimedMove(Move::kSlamTelegraph, EnemyTuning::GetInstance()->BossSlam().warningDuration);
        SetScripted(true, input.playerPosition);
        return;
    }
    if (attack == kChargeAttack) {
        BeginTimedMove(Move::kChargeTelegraph, tuning.chargeTelegraphSeconds);
        moveDirX_ = input.playerPosition.x >= boss_->GetPosition().x ? 1.0f : -1.0f;
        SetScripted(true, input.playerPosition);
        return;
    }
    if (attack == kDashAttack) {
        BeginTimedMove(Move::kDashTelegraph, tuning.dashTelegraphSeconds);
        dashLeft_ = tuning.dashCount;
        SetScripted(true, input.playerPosition);
        return;
    }
    if (attack == kWaveAttack) {
        BeginTimedMove(Move::kWaveTelegraph, tuning.waveTelegraphSeconds);
        SetScripted(true, input.playerPosition);
        return;
    }
    if (attack == kLeapAttack) {
        BeginTimedMove(Move::kLeapTelegraph, tuning.leapTelegraphSeconds);
        SetScripted(true, input.playerPosition);
        return;
    }
    if (attack == kVortexAttack) {
        // その場で身構えてから渦を作る（引き寄せと弾ける時の演出・当たり判定はシーンが出す）
        BeginTimedMove(Move::kVortexTelegraph, tuning.vortexTelegraphSeconds);
        SetScripted(true, input.playerPosition);
        return;
    }
    // ほかの名前は投げる技（volleys に書いた名前）
    const auto volley = tuning.volleys.find(attack);
    if (volley != tuning.volleys.end()) {
        volley_ = volley->second;
        BeginTimedMove(Move::kVolleyTelegraph, volley_.telegraphSeconds);
        SetScripted(true, input.playerPosition);
    }
}

/** @brief 技の後の隙に入る */
void BossBattleDirector::BeginRecover(float seconds) {
    BeginTimedMove(Move::kRecover, seconds);
}

/** @brief 時間で区切る段階に入る */
void BossBattleDirector::BeginTimedMove(Move move, float seconds) {
    move_ = move;
    moveTimer_ = seconds;
    stageSeconds_ = seconds;
}

/** @brief 今ボスが何をしているか（技・区切り・経過割合） */
BossMotionCue BossBattleDirector::CurrentMotionCue() const {
    const BossBattleTuning& tuning = BossBattleTuningStore::Get();
    // 残り秒数から、区切りの中の経過割合を出す
    const auto elapsedOf = [](float remaining, float total) {
        return total > 0.0f ? std::clamp(1.0f - remaining / total, 0.0f, 1.0f) : 1.0f;
    };
    BossMotionCue cue;
    if (phase_ == Phase::kRage) {
        cue.move = kRoarMotion;
        cue.progress = elapsedOf(phaseTimer_, tuning.rageSeconds);
        return cue;
    }
    if (staggered_) {
        cue.move = kStaggerMotion;
        cue.progress = elapsedOf(staggerTimer_, tuning.staggerSeconds);
        return cue;
    }
    cue.move = currentAttack_;
    switch (move_) {
    case Move::kChargeTelegraph:
    case Move::kDashTelegraph:
    case Move::kVolleyTelegraph:
    case Move::kWaveTelegraph:
    case Move::kLeapTelegraph:
    case Move::kSlamTelegraph:
    case Move::kVortexTelegraph:
        cue.stage = BossMotionStage::kWindup;
        cue.progress = elapsedOf(moveTimer_, stageSeconds_);
        return cue;
    case Move::kCharging:
        cue.progress = tuning.chargeDistance > 0.0f ? std::clamp(chargeTraveled_ / tuning.chargeDistance, 0.0f, 1.0f) : 1.0f;
        return cue;
    case Move::kDashing:
        cue.progress = tuning.dashSeconds > 0.0f ? std::clamp(moveTimer_ / tuning.dashSeconds, 0.0f, 1.0f) : 1.0f;
        return cue;
    case Move::kLeaping:
        cue.progress = tuning.leapSeconds > 0.0f ? std::clamp(moveTimer_ / tuning.leapSeconds, 0.0f, 1.0f) : 1.0f;
        return cue;
    case Move::kVortexPull:
        cue.progress = elapsedOf(moveTimer_, stageSeconds_);
        return cue;
    case Move::kRecover:
        cue.stage = BossMotionStage::kRecover;
        cue.progress = elapsedOf(moveTimer_, stageSeconds_);
        return cue;
    default:
        // 技を出していない間（戦う前も）は構え
        cue.move = kStanceMotion;
        return cue;
    }
}

/** @brief 投げ・衝撃波・跳びかかりの身構えと跳んでいる間を進める（扱った時は true） */
bool BossBattleDirector::UpdateRangedMove(float dt, const BossFrameInput& input, BossFrameOutput& output) {
    const BossBattleTuning& tuning = BossBattleTuningStore::Get();
    switch (move_) {
    case Move::kVolleyTelegraph:
        // 身構えている間はプレイヤーの方を向き、時間が来たら投げる
        output.warningRing = volley_.warningRing;
        SetScripted(true, input.playerPosition);
        moveTimer_ -= dt;
        if (moveTimer_ <= 0.0f) {
            output.volley = &volley_;
            BeginRecover(volley_.recoverSeconds);
        }
        return true;
    case Move::kSlamTelegraph:
        // 予告の円はシーンが出すので、ここでは時間だけ数える
        moveTimer_ -= dt;
        if (moveTimer_ <= 0.0f) {
            BeginRecover(tuning.slamRecoverSeconds);
        }
        return true;
    case Move::kWaveTelegraph:
        output.warningRing = tuning.waveWarningRing;
        moveTimer_ -= dt;
        if (moveTimer_ <= 0.0f) {
            output.waveStarted = true;
            BeginRecover(tuning.waveRecoverSeconds);
        }
        return true;
    case Move::kLeapTelegraph:
        output.warningRing = tuning.leapWarningRing;
        SetScripted(true, input.playerPosition);
        moveTimer_ -= dt;
        if (moveTimer_ <= 0.0f) {
            // プレイヤーの真上へ跳ぶ（そこに立てなければその場で跳ぶ）
            leapFrom_ = boss_->GetPosition();
            leapTo_ = leapFrom_;
            leapTo_.x = input.playerPosition.x;
            if (!input.canSeePlayer || input.isBlocked(leapTo_)) {
                leapTo_ = leapFrom_;
            }
            move_ = Move::kLeaping;
            moveTimer_ = 0.0f;
        }
        return true;
    case Move::kVortexTelegraph:
        output.warningRing = tuning.vortexWarningRing;
        moveTimer_ -= dt;
        if (moveTimer_ <= 0.0f) {
            BeginTimedMove(Move::kVortexPull, tuning.vortexPullSeconds);
        }
        return true;
    case Move::kVortexPull:
        // 引き寄せている間はシーンへ知らせ続け、時間が来たら弾けさせる
        moveTimer_ -= dt;
        output.vortexPulling = true;
        output.vortexProgress = stageSeconds_ > 0.0f ? std::clamp(1.0f - moveTimer_ / stageSeconds_, 0.0f, 1.0f) : 1.0f;
        if (moveTimer_ <= 0.0f) {
            output.vortexBurst = true;
            BeginRecover(tuning.vortexRecoverSeconds);
        }
        return true;
    case Move::kLeaping: {
        // 放物線で跳び、着地したら周りへのダメージを頼む
        moveTimer_ += dt;
        const float t = tuning.leapSeconds > 0.0f ? std::clamp(moveTimer_ / tuning.leapSeconds, 0.0f, 1.0f) : 1.0f;
        Vector3 position = leapFrom_;
        position.x += (leapTo_.x - leapFrom_.x) * t;
        position.y += tuning.leapHeight * kLeapArcScale * t * (1.0f - t);
        boss_->SetPosition(position);
        if (t >= 1.0f) {
            output.leapLanded = true;
            BeginRecover(tuning.leapRecoverSeconds);
        }
        return true;
    }
    default:
        return false;
    }
}

/** @brief 突進・ダッシュ・隙を1フレーム進める */
void BossBattleDirector::UpdateMove(float dt, const BossFrameInput& input, BossFrameOutput& output) {
    const BossBattleTuning& tuning = BossBattleTuningStore::Get();
    if (UpdateRangedMove(dt, input, output)) {
        return;
    }
    switch (move_) {
    case Move::kChargeTelegraph:
        // 身構えて、向きを決めたら真っすぐ走る
        output.chargeTelegraph = true;
        moveTimer_ -= dt;
        if (moveTimer_ <= 0.0f) {
            move_ = Move::kCharging;
            chargeTraveled_ = 0.0f;
        }
        break;
    case Move::kCharging: {
        // 壁に当たるか、決めた距離を走り切ったら止まる
        const float step = tuning.chargeSpeed * dt;
        Vector3 next = boss_->GetPosition();
        next.x += moveDirX_ * step;
        if (chargeTraveled_ >= tuning.chargeDistance || input.isBlocked(next)) {
            BeginRecover(tuning.chargeRecoverSeconds);
            break;
        }
        boss_->SetPosition(next);
        chargeTraveled_ += step;
        output.rushing = !strikeLanded_;
        output.charging = true;
        output.rushDirX = moveDirX_;
        output.strikeDamage = tuning.chargeDamage;
        break;
    }
    case Move::kDashTelegraph:
        // 身構えてから、プレイヤーの向こう側へ回り込む
        output.dashTelegraph = true;
        SetScripted(true, input.playerPosition);
        moveTimer_ -= dt;
        if (moveTimer_ <= 0.0f) {
            const float side = boss_->GetPosition().x <= input.playerPosition.x ? 1.0f : -1.0f;
            dashFrom_ = boss_->GetPosition();
            dashTo_ = dashFrom_;
            dashTo_.x = input.playerPosition.x + side * tuning.dashOffset;
            // 行き先が壁の中なら、プレイヤーの手前で止める
            if (input.isBlocked(dashTo_)) {
                dashTo_.x = input.playerPosition.x - side * tuning.dashOffset;
            }
            move_ = Move::kDashing;
            moveTimer_ = 0.0f;
            strikeLanded_ = false;
        }
        break;
    case Move::kDashing: {
        moveTimer_ += dt;
        const float t = tuning.dashSeconds > 0.0f ? std::clamp(moveTimer_ / tuning.dashSeconds, 0.0f, 1.0f) : 1.0f;
        Vector3 next = dashFrom_;
        next.x += (dashTo_.x - dashFrom_.x) * EaseOutQuad(t);
        // 壁や足場の端に当たったら、その手前でこの1回を終える（時間だけ進めて壁の向こうへ抜けないように）
        const bool blocked = input.isBlocked(next);
        if (!blocked) {
            boss_->SetPosition(next);
        }
        output.rushing = !strikeLanded_;
        output.rushDirX = dashTo_.x >= dashFrom_.x ? 1.0f : -1.0f;
        output.strikeDamage = tuning.dashDamage;
        if (t >= 1.0f || blocked) {
            --dashLeft_;
            if (dashLeft_ > 0) {
                BeginTimedMove(Move::kDashTelegraph, tuning.dashTelegraphSeconds);
            } else {
                BeginRecover(tuning.dashRecoverSeconds);
            }
        }
        break;
    }
    case Move::kRecover:
        // 隙が終わったらふだんの動きへ戻す
        moveTimer_ -= dt;
        if (moveTimer_ <= 0.0f) {
            move_ = Move::kNone;
            SetScripted(false, input.playerPosition);
        }
        break;
    default:
        // 何もしていない時と、投げ・衝撃波・跳びかかり（UpdateRangedMove で進める）はここでは扱わない
        break;
    }
}

} // namespace engine::game
