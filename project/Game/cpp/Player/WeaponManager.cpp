/**
 * @file WeaponManager.cpp
 * @brief 武器の一覧の読み込みと、装備・入手の管理（WeaponManager）の実装
 */
#include "WeaponManager.h"
#include "GameRules.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "StringUtility.h"
#include <algorithm>
#include <iterator>

namespace engine::game {
namespace {
constexpr float kMinSkillCooldownMult = 0.05f; // 覚醒中に固有技を連打できなくなる下限
constexpr size_t kColorChannels = 4;
constexpr int kMaxEffectBurstCount = 64; // 1ヒットで出す属性粒子の上限（描画が詰まらないように）

// 1つの値で済む項目の対応表（日本語名・種類・色・コマンド表は変換が要るので個別に読む）
constexpr JsonField<RangedWeaponData> kRangedFields[] = {
    { nullptr, "name", &RangedWeaponData::name },
    { nullptr, "damage", &RangedWeaponData::damage },
    { nullptr, "range", &RangedWeaponData::range },
    { nullptr, "attackInterval", &RangedWeaponData::attackInterval },
    { nullptr, "description", &RangedWeaponData::description },
    { nullptr, "element", &RangedWeaponData::element },
};

constexpr JsonField<WeaponData> kWeaponFields[] = {
    { nullptr, "name", &WeaponData::name },
    { nullptr, "damage", &WeaponData::damage },
    { nullptr, "range", &WeaponData::range },
    { nullptr, "attackInterval", &WeaponData::attackInterval },
    { nullptr, "description", &WeaponData::description },
    { nullptr, "knockbackMult", &WeaponData::knockbackMult },
    { nullptr, "element", &WeaponData::element },
    { nullptr, "hitStyle", &WeaponData::hitStyle },
    { "effect", "burstCount", &WeaponData::effectBurstCount },
    { "effect", "ringRadius", &WeaponData::effectRingRadius },
};

constexpr JsonField<AwakenedWeaponMods> kAwakenedFields[] = {
    { nullptr, "damageMult", &AwakenedWeaponMods::damageMult },
    { nullptr, "skillRadiusMult", &AwakenedWeaponMods::skillRadiusMult },
    { nullptr, "skillCooldownMult", &AwakenedWeaponMods::skillCooldownMult },
    { nullptr, "knockbackMult", &AwakenedWeaponMods::knockbackMult },
};

/** @brief [R, G, B, A] の配列を読む（足りない成分は out の値のまま） */
void ReadColor(const nlohmann::json& value, float (&out)[kColorChannels]) {
    if (!value.is_array()) {
        return;
    }
    // 数字でない成分は読まない（そのまま取り出すと例外で止まる）
    for (size_t i = 0; i < kColorChannels && i < value.size(); ++i) {
        if (value[i].is_number()) {
            out[i] = value[i].get<float>();
        }
    }
}

constexpr const char* kWeaponDataPath = "Resources/Config/weapons.json";

/** @brief weapons.json の銃の種類名を GunType へ変換する */
GunType ParseGunType(const std::string& type) {
    /** @brief 銃種と設定ファイル内の識別名の対応 */
    struct NamedGunType {
        const char* name;
        GunType type;
    };
    static constexpr NamedGunType kNames[] = {
        { "Magnum", GunType::kMagnum },
        { "SMG", GunType::kSMG },
        { "Shotgun", GunType::kShotgun },
        { "Railgun", GunType::kRailgun },
    };
    for (const NamedGunType& entry : kNames) {
        if (type == entry.name) {
            return entry.type;
        }
    }
    return GunType::kPistol;
}

/** @brief JSON配列からコマンド入力の一覧を読む */
std::vector<WeaponCommand> ParseCommands(const nlohmann::json& arr) {
    std::vector<WeaponCommand> commands;
    for (const auto& c : arr) {
        commands.push_back({ c.value("key", ""),
            StringUtility::ConvertString(c.value("desc", "")) });
    }
    return commands;
}

} // namespace

/** @brief シングルトンインスタンスを取得する */
WeaponManager* WeaponManager::GetInstance() {
    static WeaponManager sInstance;
    return &sInstance;
}

/** @brief 画面に出す武器名を返す（剣・槍などの武器の種類の名前） */
std::wstring WeaponManager::GetDisplayName(const WeaponData& data) {
    return WeaponTypeLabel(data.type);
}

/** @brief weapons.json を読み込んで武器の一覧を作る */
WeaponManager::WeaponManager() {
    nlohmann::json j = JsonHelper::Load(kWeaponDataPath);

    // 射撃武器一覧（Gキーで循環切り替え、スタイルとは独立に選ぶ）
    for (const auto& r : j.value("rangedWeapons", nlohmann::json::array())) {
        RangedWeaponData data {};
        data.type = ParseGunType(r.value("type", ""));
        // 遠距離用の補助武器は、役割が重なる複数の銃から一つに集約する。
        if (data.type != GunType::kPistol) {
            continue;
        }
        ReadJsonFields(r, kRangedFields, data);
        data.nameJp = StringUtility::ConvertString(r.value("nameJp", ""));
        data.commands = ParseCommands(r.value("commands", nlohmann::json::array()));

        // 色は白から、属性色は未指定ならマズルの色を引き継ぐ
        std::fill(std::begin(data.color), std::end(data.color), 1.0f);
        ReadColor(r.value("color", nlohmann::json::array()), data.color);
        std::copy(std::begin(data.color), std::end(data.color), std::begin(data.effectColor));
        ReadColor(r.value("effectColor", nlohmann::json::array()), data.effectColor);

        rangedWeapons_.push_back(std::move(data));
    }
    // JSON が空でも GetRanged() が範囲外参照しないよう保険を入れておく
    if (rangedWeapons_.empty()) {
        constexpr float kFallbackDamage = 12.0f;
        constexpr float kFallbackRange = 15.0f;
        constexpr float kFallbackInterval = 0.18f;
        rangedWeapons_.push_back({ "Handgun", L"ハンドガン", GunType::kPistol,
            kFallbackDamage, kFallbackRange, kFallbackInterval, "", { 1.0f, 1.0f, 1.0f, 1.0f }, { } });
    }

    // スタイル一覧（1〜5キー対応）
    for (const auto& w : j.value("weapons", nlohmann::json::array())) {
        WeaponData data {};
        data.type = ParseWeaponTypeName(w.value("type", ""));
        // 本編で使う四つの基本戦闘スタイルだけを読み込む（同時に持てるのはkSlotCount個まで）
        static constexpr WeaponType kCoreWeaponTypes[] = {
            WeaponType::kSword, WeaponType::kSpear, WeaponType::kDagger, WeaponType::kHammer,
        };
        const bool isCoreWeapon = std::find(std::begin(kCoreWeaponTypes), std::end(kCoreWeaponTypes), data.type)
            != std::end(kCoreWeaponTypes);
        if (!isCoreWeapon) {
            continue;
        }
        ReadJsonFields(w, kWeaponFields, data);
        data.commands = ParseCommands(w.value("commands", nlohmann::json::array()));
        ReadJsonFields(w.value("awakened", nlohmann::json::object()), kAwakenedFields, data.awakened);
        // 読んだ値を使える範囲に収める
        data.awakened.skillCooldownMult = (std::max)(data.awakened.skillCooldownMult, kMinSkillCooldownMult);
        data.effectBurstCount = std::clamp(data.effectBurstCount, 0, kMaxEffectBurstCount);
        data.effectRingRadius = (std::max)(data.effectRingRadius, 0.0f);

        // 色は黒から、属性色は未指定ならスタイルの色を引き継ぐ
        std::fill(std::begin(data.styleColor), std::end(data.styleColor), 0.0f);
        data.styleColor[kColorChannels - 1] = 1.0f;
        ReadColor(w.value("styleColor", nlohmann::json::array()), data.styleColor);
        std::copy(std::begin(data.styleColor), std::end(data.styleColor), std::begin(data.effectColor));
        ReadColor(w.value("effect", nlohmann::json::object()).value("color", nlohmann::json::array()), data.effectColor);

        weapons_.push_back(std::move(data));
    }

    // スロットは倒した敵から奪って埋めていく想定なので、初期状態は全ロック。
    // ただし何も使えないと詰むため、機動力型（奇術師/Dagger）だけ最初から解放しておく
    unlocked_.assign(weapons_.size(), false);
}

/** @brief 指定したスロットを選択する */
void WeaponManager::SelectSlot(int slot) {
    if (!IsSlotFilled(slot)) {
        return;
    }
    selectedSlot_ = slot;
    index_ = GetSlotWeaponIndex(slot);
}

/** @brief スロットに登録された武器インデックスを返す（ボスの枠はボスの武器） */
int WeaponManager::GetSlotWeaponIndex(int slot) const {
    if (slot == kBossSlot) {
        return bossWeaponIndex_;
    }
    if (slot < 0 || slot >= static_cast<int>(slots_.size())) {
        return -1;
    }
    return slots_[slot];
}

/** @brief その種類の武器をどれかのスロットに持っているか */
bool WeaponManager::Owns(WeaponType type) const {
    return std::any_of(slots_.begin(), slots_.end(), [this, type](int index) {
        return index >= 0 && index < static_cast<int>(weapons_.size()) && weapons_[index].type == type;
    });
}

/** @brief 次の解放済みスタイルへ切り替える（循環、未解放はスキップ） */
void WeaponManager::SelectNext() {
    // ボスの枠もふつうの枠の後ろとして順に回す
    const int count = kBossSlot + 1;
    for (int step = 1; step <= count; ++step) {
        const int slot = (selectedSlot_ + step) % count;
        if (IsSlotFilled(slot)) {
            SelectSlot(slot);
            return;
        }
    }
}

/** @brief 前の解放済みスタイルへ切り替える（循環、未解放はスキップ） */
void WeaponManager::SelectPrev() {
    // ボスの枠もふつうの枠の後ろとして順に回す
    const int count = kBossSlot + 1;
    for (int step = 1; step <= count; ++step) {
        const int slot = ((selectedSlot_ - step) % count + count) % count;
        if (IsSlotFilled(slot)) {
            SelectSlot(slot);
            return;
        }
    }
}

/** @brief 指定タイプの武器を解放し、そのまま装備する（敵からの武器奪取用） */
bool WeaponManager::Unlock(WeaponType type) {
    return Acquire(type) == AcquireResult::kAdded;
}

/** @brief 全解放済み武器を対象に次を選び、現在スロットへ割り当てる */
void WeaponManager::SelectNextUnlockedInCurrentSlot() {
    SelectUnlockedInCurrentSlot(1);
}

/** @brief 全解放済み武器を対象に前を選び、現在スロットへ割り当てる */
void WeaponManager::SelectPrevUnlockedInCurrentSlot() {
    SelectUnlockedInCurrentSlot(-1);
}

/** @brief 解放済みの武器を direction（+1 で次、-1 で前）の向きに探し、現在のスロットへ割り当てる */
void WeaponManager::SelectUnlockedInCurrentSlot(int direction) {
    const int count = static_cast<int>(weapons_.size());
    for (int step = 1; step <= count; ++step) {
        // 負の余りにならないよう count を足してから割る
        const int candidate = ((index_ + direction * step) % count + count) % count;
        if (!IsUnlocked(candidate)) {
            continue;
        }
        if (selectedSlot_ < 0 || selectedSlot_ >= static_cast<int>(slots_.size())) {
            selectedSlot_ = 0;
        }
        // ほかの枠に入っている武器は選ばない（同じ武器が2つの枠に並ばないように）
        bool inOtherSlot = false;
        for (int slot = 0; slot < static_cast<int>(slots_.size()); ++slot) {
            inOtherSlot = inOtherSlot || (slot != selectedSlot_ && slots_[slot] == candidate);
        }
        if (inOtherSlot) {
            continue;
        }
        index_ = candidate;
        slots_[selectedSlot_] = candidate;
        fusions_[selectedSlot_] = Fusion {};
        return;
    }
}


/** @brief 武器を空きスロットへ追加し、満杯なら交換待ちにする */
WeaponManager::AcquireResult WeaponManager::Acquire(WeaponType type) {
    for (size_t i = 0; i < weapons_.size(); ++i) {
        if (weapons_[i].type != type) {
            continue;
        }
        if (unlocked_[i]) {
            return AcquireResult::kDuplicate;
        }

        for (int slot = 0; slot < static_cast<int>(slots_.size()); ++slot) {
            if (slots_[slot] >= 0) {
                continue;
            }
            slots_[slot] = static_cast<int>(i);
            unlocked_[i] = true;
            selectedSlot_ = slot;
            index_ = static_cast<int>(i);
            return AcquireResult::kAdded;
        }

        pendingWeaponIndex_ = static_cast<int>(i);
        return AcquireResult::kNeedsReplacement;
    }
    return AcquireResult::kDuplicate;
}

/** @brief 交換待ちの武器で指定スロットを置き換える */
void WeaponManager::ReplacePendingWeapon(int slot) {
    if (!HasPendingWeapon() || slot < 0 || slot >= static_cast<int>(slots_.size())) {
        return;
    }

    const int removed = slots_[slot];
    if (removed >= 0) {
        unlocked_[removed] = false;
    }
    fusions_[slot] = Fusion {};
    slots_[slot] = pendingWeaponIndex_;
    unlocked_[pendingWeaponIndex_] = true;
    selectedSlot_ = slot;
    index_ = pendingWeaponIndex_;
    pendingWeaponIndex_ = -1;
}

/** @brief 交換待ちの武器を破棄する */
void WeaponManager::DiscardPendingWeapon() {
    pendingWeaponIndex_ = -1;
}

/** @brief 道中で奪った武器を合体させるふつうの枠を選ぶ（選んでいる枠がふつうの枠ならそこ、ボスの枠なら合体していないふつうの枠） */
int WeaponManager::FindFusionSlot() const {
    const auto matches = [this](int slot) {
        return slot >= 0 && slot < kSlotCount && slots_[slot] >= 0;
    };
    if (matches(selectedSlot_)) {
        return selectedSlot_;
    }
    // 選んでいる枠が合わなければ、合体していない枠を先に選ぶ（合体中の耐久をむだに戻さない）
    int fallback = -1;
    for (int slot = 0; slot < kSlotCount; ++slot) {
        if (!matches(slot)) {
            continue;
        }
        if (!IsFusionValid(slot)) {
            return slot;
        }
        if (fallback < 0) {
            fallback = slot;
        }
    }
    return fallback;
}

/** @brief 指定した枠の武器に相棒を合体させる（耐久を満タンにする） */
void WeaponManager::SetFusion(int slot, int partnerIndex, int durability) {
    // 相棒の武器は枠には入れない（持っている武器の数は増えない）。すでに合体していれば相棒を入れ替えて耐久も戻す
    Fusion& fusion = fusions_[slot];
    fusion.baseIndex = slots_[slot];
    fusion.partnerIndex = partnerIndex;
    fusion.maxDurability = (std::max)(durability, 1);
    fusion.durability = fusion.maxDurability;
}

/** @brief 交換待ちの武器を、指定した枠の武器に合体させる */
bool WeaponManager::FusePendingInto(int slot, int durability) {
    if (!HasPendingWeapon() || slot < 0 || slot >= kSlotCount || slots_[slot] < 0) {
        return false;
    }
    SetFusion(slot, pendingWeaponIndex_, durability);
    pendingWeaponIndex_ = -1;
    return true;
}

/** @brief 指定した種類の武器を、指定した枠の武器に合体させる（もう持っている種類を奪った時に使う） */
bool WeaponManager::FuseInto(int slot, WeaponType type, int durability) {
    if (slot < 0 || slot >= kSlotCount || slots_[slot] < 0) {
        return false;
    }
    const auto found = std::find_if(weapons_.begin(), weapons_.end(), [type](const WeaponData& data) {
        return data.type == type;
    });
    if (found == weapons_.end()) {
        return false;
    }
    // 同じ種類どうしでも合体できる（剣＋剣）
    SetFusion(slot, static_cast<int>(std::distance(weapons_.begin(), found)), durability);
    return true;
}

/** @brief ボスから奪った武器をボスの枠へ入れる（ボスの枠の武器は入れ替わらず、合体しても砕けない） */
WeaponManager::BossAcquireResult WeaponManager::AcquireBossWeapon(WeaponType type) {
    const auto found = std::find_if(weapons_.begin(), weapons_.end(), [type](const WeaponData& data) {
        return data.type == type;
    });
    if (found == weapons_.end()) {
        return BossAcquireResult::kSame;
    }
    const int index = static_cast<int>(std::distance(weapons_.begin(), found));
    // 空いていれば入れて、そのまま持ち替える
    if (bossWeaponIndex_ < 0) {
        bossWeaponIndex_ = index;
        bossPartnerIndex_ = -1;
        SelectSlot(kBossSlot);
        return BossAcquireResult::kAdded;
    }
    // 主軸か相棒と同じ種類なら、今のまま
    if (index == bossWeaponIndex_ || index == bossPartnerIndex_) {
        return BossAcquireResult::kSame;
    }
    pendingBossIndex_ = index;
    return BossAcquireResult::kNeedsMainChoice;
}

/** @brief 合体の主軸を決める（選ばなかった方が相棒になり、前の相棒は外れる） */
void WeaponManager::ChooseBossMain(bool keepCurrentMain) {
    if (!HasPendingBossChoice() || bossWeaponIndex_ < 0) {
        pendingBossIndex_ = -1;
        return;
    }
    if (keepCurrentMain) {
        bossPartnerIndex_ = pendingBossIndex_;
    } else {
        bossPartnerIndex_ = bossWeaponIndex_;
        bossWeaponIndex_ = pendingBossIndex_;
    }
    pendingBossIndex_ = -1;
    // 主軸が替わったかもしれないので、ボスの枠を選んで持ち替える
    SelectSlot(kBossSlot);
}

/** @brief すべての枠に武器が入っているか */
bool WeaponManager::AreSlotsFull() const {
    return std::all_of(slots_.begin(), slots_.end(), [](int index) {
        return index >= 0;
    });
}

/** @brief 指定した枠の合体の状態が今も有効か（枠の武器が合体した時のままか） */
bool WeaponManager::IsFusionValid(int slot) const {
    if (slot < 0 || slot >= kSlotCount) {
        return false;
    }
    const Fusion& fusion = fusions_[slot];
    return fusion.partnerIndex >= 0 && fusion.partnerIndex < static_cast<int>(weapons_.size())
        && fusion.baseIndex >= 0 && slots_[slot] == fusion.baseIndex && fusion.durability > 0;
}

/** @brief 指定した枠の武器に合体している相棒の武器の番号を返す（合体していなければ -1） */
int WeaponManager::GetFusedWeaponIndex(int slot) const {
    if (slot == kBossSlot) {
        return bossWeaponIndex_ >= 0 ? bossPartnerIndex_ : -1;
    }
    return IsFusionValid(slot) ? fusions_[slot].partnerIndex : -1;
}

/** @brief 指定した枠の合体の残り耐久の割合を返す（0〜1、合体していなければ 0） */
float WeaponManager::GetFusionDurabilityRatio(int slot) const {
    if (!IsFusionValid(slot)) {
        return 0.0f;
    }
    const Fusion& fusion = fusions_[slot];
    return static_cast<float>(fusion.durability) / static_cast<float>(fusion.maxDurability);
}

/** @brief 選んでいる枠の武器に合体している相棒の武器（合体していなければ nullptr） */
const WeaponData* WeaponManager::GetCurrentFusionPartner() const {
    const int partner = GetFusedWeaponIndex(selectedSlot_);
    return partner >= 0 ? &weapons_[partner] : nullptr;
}

/** @brief 選んでいる枠の合体の耐久を減らす。尽きたら枠の武器ごと砕き、ほかの枠の武器へ持ち替える */
bool WeaponManager::ConsumeFusionDurability(int amount) {
    if (!IsFusionValid(selectedSlot_)) {
        return false;
    }
    Fusion& fusion = fusions_[selectedSlot_];
    fusion.durability -= (std::max)(amount, 0);
    if (fusion.durability > 0) {
        return false;
    }
    // 土台の武器も砕けて枠が空く（合体した時は枠が満杯なので、ほかの枠の武器は必ず残る）
    unlocked_[slots_[selectedSlot_]] = false;
    slots_[selectedSlot_] = -1;
    fusion = Fusion {};
    SelectNext();
    return true;
}

/** @brief すべての枠の合体を解く */
void WeaponManager::ClearFusions() {
    fusions_.fill(Fusion {});
}

/** @brief 全武器を解放し、先頭からスロット数ぶんを登録する */
void WeaponManager::UnlockAll() {
    // 全武器を使える状態にするのは常に安全（現在の装備選択には影響しない）
    unlocked_.assign(weapons_.size(), true);

    // 既に何か装備済み（Trainingで組んだ構成など）ならスロット構成は壊さない。
    // ここで毎回スロットを既定値へ戻すと、Trainingで選んだ武器がテストシーンへ行くたびに
    // リセットされてしまい、Training→テストという行き来での検証ができなくなる
    if (HasEquippedWeapon()) {
        return;
    }

    slots_.fill(-1);
    for (int slot = 0; slot < static_cast<int>(slots_.size())
        && slot < static_cast<int>(weapons_.size());
        ++slot) {
        slots_[slot] = slot;
    }
    selectedSlot_ = 0;
    index_ = 0;
    pendingWeaponIndex_ = -1;
}

/** @brief トレーニング用に指定武器を現在のスロットへ装備する */
void WeaponManager::EquipForTraining(WeaponType type) {
    // 指定された種類の武器を探す
    for (int weaponIndex = 0; weaponIndex < static_cast<int>(weapons_.size()); ++weaponIndex) {
        if (weapons_[weaponIndex].type != type) {
            continue;
        }

        // もうスロットに入っていれば、そのスロットを選ぶだけ
        for (int slot = 0; slot < static_cast<int>(slots_.size()); ++slot) {
            if (slots_[slot] == weaponIndex) {
                selectedSlot_ = slot;
                index_ = weaponIndex;
                pendingWeaponIndex_ = -1;
                return;
            }
        }

        // 空きスロットへ入れる。空きが無ければ今選んでいるスロット（無ければ先頭）と入れ替える
        int targetSlot = -1;
        for (int slot = 0; slot < static_cast<int>(slots_.size()); ++slot) {
            if (slots_[slot] < 0) {
                targetSlot = slot;
                break;
            }
        }
        if (targetSlot < 0 || targetSlot >= static_cast<int>(slots_.size())) {
            targetSlot = (selectedSlot_ >= 0 && selectedSlot_ < static_cast<int>(slots_.size()))
                ? selectedSlot_
                : 0;
        }
        // 押し出した武器は未解放に戻し、入れた武器を解放して選ぶ
        const int removedWeapon = slots_[targetSlot];
        if (removedWeapon >= 0 && removedWeapon != weaponIndex) {
            unlocked_[removedWeapon] = false;
        }
        slots_[targetSlot] = weaponIndex;
        fusions_[targetSlot] = Fusion {};
        unlocked_[weaponIndex] = true;
        selectedSlot_ = targetSlot;
        index_ = weaponIndex;
        pendingWeaponIndex_ = -1;
        return;
    }
}

/** @brief タイトルデモ用に指定武器を装備する（スロットから押し出した武器も解放済みのまま残す） */
void WeaponManager::EquipForDemo(WeaponType type) {
    const std::vector<bool> unlocked = unlocked_;
    EquipForTraining(type);
    unlocked_ = unlocked;
    unlocked_[index_] = true;
}

/** @brief 全武器を未解放状態へ戻す（新しいランの開始時に呼ぶ） */
void WeaponManager::Reset() {
    std::fill(unlocked_.begin(), unlocked_.end(), false);
    slots_.fill(-1);
    ClearFusions();
    bossWeaponIndex_ = -1;
    bossPartnerIndex_ = -1;
    pendingBossIndex_ = -1;
    selectedSlot_ = -1;
    pendingWeaponIndex_ = -1;
    index_ = 0;
    rangedIndex_ = 0;
}

/** @brief 新しいランの開始時に呼ぶ。Reset()した上でgame_rules.jsonのstarterWeaponを最初のスロットへ装備する */
void WeaponManager::ResetForNewRun() {
    Reset();
    const std::string& starter = GameRules::GetInstance()->Get().starterWeapon;
    if (!starter.empty()) {
        Acquire(ParseWeaponTypeName(starter));
    }
}

/** @brief 現在の解放状況・装備状態を退避する */
WeaponManager::Snapshot WeaponManager::SaveSnapshot() const {
    return { unlocked_, slots_, index_, selectedSlot_, pendingWeaponIndex_, rangedIndex_, fusions_, bossWeaponIndex_, bossPartnerIndex_ };
}

/** @brief SaveSnapshot()で退避した状態へ戻す */
void WeaponManager::RestoreSnapshot(const Snapshot& snapshot) {
    unlocked_ = snapshot.unlocked;
    slots_ = snapshot.slots;
    // 合体も退避した時のものへ戻す（枠の武器が違っていれば IsFusionValid が無効として扱う）
    fusions_ = snapshot.fusions;
    // ボスの枠は番号が武器の一覧の外なら空にする（相棒は主軸がある時だけ残す）
    const auto validIndex = [this](int index) {
        return index >= 0 && index < static_cast<int>(weapons_.size()) ? index : -1;
    };
    bossWeaponIndex_ = validIndex(snapshot.bossWeapon);
    bossPartnerIndex_ = bossWeaponIndex_ >= 0 ? validIndex(snapshot.bossPartner) : -1;
    pendingBossIndex_ = -1;
    index_ = snapshot.index;
    selectedSlot_ = snapshot.selectedSlot;
    pendingWeaponIndex_ = snapshot.pendingWeaponIndex;
    rangedIndex_ = snapshot.rangedIndex;
    // 保存データ内の番号を、現在の武器定義の範囲へ制限する
    unlocked_.resize(weapons_.size(), false);
    for (int& slot : slots_) {
        if (!IsUnlocked(slot)) {
            slot = -1;
        }
    }
    if (index_ < 0 || index_ >= static_cast<int>(weapons_.size())) {
        index_ = 0;
    }
    if (!IsSlotFilled(selectedSlot_)) {
        selectedSlot_ = -1;
    } else {
        index_ = GetSlotWeaponIndex(selectedSlot_);
    }
    if (pendingWeaponIndex_ < 0 || pendingWeaponIndex_ >= static_cast<int>(weapons_.size())) {
        pendingWeaponIndex_ = -1;
    }
    if (rangedIndex_ < 0 || rangedIndex_ >= static_cast<int>(rangedWeapons_.size())) {
        rangedIndex_ = 0;
    }
}

} // namespace engine::game
