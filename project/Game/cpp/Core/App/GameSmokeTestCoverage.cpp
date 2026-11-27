/**
 * @file GameSmokeTestCoverage.cpp
 * @brief 自動テストプレイで、ふつうの武器を順に渡す手順（grantWeapon）と、まだ試していない物を狙う手順（coverGaps）を実装するファイル
 * @note 狙う物は網羅の一覧（TestCoverage）から読むので、武器・コンボの段・とどめの型を足しても手順の表を書き足さなくてよい。
 */
#include "GameSmokeTest.h"
#include "BossExecutionTuning.h"
#include "Input.h"
#include "MeleeCombo.h"
#include "StageEditor.h"
#include "TestCoverage.h"
#include "Weapon.h"
#include "WeaponManager.h"
#include <set>

namespace engine::game {
namespace {
constexpr const char* kMeleeStepCategory = "melee_step"; // 網羅の、コンボの段の種類の名前
constexpr const char* kExecutionCategory = "execution"; // 網羅の、とどめの型の種類の名前
constexpr const char* kOnlyCombo = "combo"; // coverGaps の only にこう書くと、コンボの段だけを狙う
constexpr const char* kOnlyExecution = "execution"; // coverGaps の only にこう書くと、とどめの型だけを狙う
constexpr int kReplaceSlot = 0; // 枠が満杯の時に入れ替える枠
constexpr int kSelectPressFrame = 1; // 渡した次のフレームに枠の数字キーを押す
constexpr int kSelectFrames = 3; // 渡してから持ち替えが済むまでのフレーム数（押した次のフレームに離す）

/** @brief 枠の数字キー（ふつうの枠は 1〜3、ボスの枠は 4） */
uint8_t SlotKey(int slot) {
    return static_cast<uint8_t>(DIK_1 + slot);
}

/**
 * @brief 渡した武器の枠へ、数字キーを押して持ち替える（本物の操作と同じ経路で、技の途中の状態も仕切り直させる）
 * @param runner キーを押したことにする進行役
 * @param slot 持ち替える枠（-1 なら何もしない）
 * @param frame 渡してからのフレーム数（1 で押し、2 で離す）
 */
void PressSlotKey(SmokeTestRunner& runner, int slot, int frame) {
    if (slot < 0) {
        return;
    }
    runner.InjectKey(SlotKey(slot), frame == kSelectPressFrame);
}

/** @brief 網羅の種類の中で、試せるのにまだ試していない名前を集める */
std::set<std::string> UntriedNames(const std::string& category) {
    std::set<std::string> untried;
    testCoverage::ForEach([&](const std::string& name, const std::set<std::string>& declared, const std::set<std::string>& covered) {
        if (name != category) {
            return;
        }
        for (const std::string& item : declared) {
            if (covered.count(item) == 0) {
                untried.insert(item);
            }
        }
    });
    return untried;
}

/** @brief コンボ一式に、まだ試していない段が入っているか */
bool HasUntriedStep(const MeleeComboSet& set, const std::set<std::string>& untried) {
    const auto isUntried = [&untried](const MeleeAttackDef* attack) {
        return attack != nullptr && untried.count(attack->id) > 0;
    };
    for (int i = 0; i < set.ground.count; ++i) {
        if (isUntried(&set.ground[i])) {
            return true;
        }
    }
    for (int i = 0; i < set.air.count; ++i) {
        if (isUntried(&set.air[i])) {
            return true;
        }
    }
    return isUntried(set.launcher);
}
} // namespace

/** @brief ふつうの武器を渡して枠に入れる（もう持っていればその枠、枠が満杯なら1つ目の枠と入れ替える） */
int GameSmokeTest::GrantWeapon(const std::string& weaponName) {
    WeaponManager* weapons = WeaponManager::GetInstance();
    const auto& list = weapons->GetList();
    if (list.empty()) {
        runner_.Fail("grantWeapon: 武器の一覧が空です");
        return -1;
    }
    const WeaponType type = weaponName == kNextWeaponName ? list[weaponGrantRotation_++ % list.size()].type
                                                          : ParseWeaponTypeName(weaponName);
    // その種類を入れている枠を探す
    const auto findSlot = [weapons, &list, type]() {
        for (int slot = 0; slot < WeaponManager::kSlotCount; ++slot) {
            const int index = weapons->GetSlotWeaponIndex(slot);
            if (index >= 0 && index < static_cast<int>(list.size()) && list[static_cast<size_t>(index)].type == type) {
                return slot;
            }
        }
        return -1;
    };
    int slot = findSlot();
    if (slot < 0) {
        weapons->Acquire(type);
        // 枠が満杯なら交換待ちになるので、決まった枠と入れ替える
        if (weapons->HasPendingWeapon()) {
            weapons->ReplacePendingWeapon(kReplaceSlot);
        }
        slot = findSlot();
    }
    runner_.AddLog("    武器を渡す（" + std::string(WeaponTypeName(type))
        + (slot >= 0 ? "、枠 " + std::to_string(slot + 1) + "）" : "、渡せなかった）"));
    return slot;
}

/** @brief grantWeapon の手順を1フレーム進める（渡した後、その枠の数字キーで持ち替える。終わったら true） */
bool GameSmokeTest::UpdateGrantWeapon(const SmokeTestStep& step) {
    if (runner_.GetStepFrame() == 0) {
        grantedSlot_ = GrantWeapon(WeaponName(step));
        return grantedSlot_ < 0;
    }
    PressSlotKey(runner_, grantedSlot_, runner_.GetStepFrame());
    return runner_.GetStepFrame() + 1 >= kSelectFrames;
}

/** @brief まだ試していないコンボの段ととどめの型から、狙う物の並びを作る（武器の表の順。とどめは最後） */
void GameSmokeTest::BuildGapTargets(const std::string& only) {
    gapTargets_.clear();
    const auto& list = WeaponManager::GetInstance()->GetList();
    const bool combos = only.empty() || only == kOnlyCombo;
    const bool executions = only.empty() || only == kOnlyExecution;
    // コンボの段: まだ試していない段を持つ武器ごとに1つ（ふつうの枠とボスの枠は別のコンボなので別に狙う）
    const std::set<std::string> untriedSteps = combos ? UntriedNames(kMeleeStepCategory) : std::set<std::string>();
    for (const auto& weapon : list) {
        const std::string name = WeaponTypeName(weapon.type);
        if (HasUntriedStep(GetMeleeComboSet(weapon.type), untriedSteps)) {
            gapTargets_.push_back({ name, false, false, name + " のコンボの段" });
        }
        const MeleeComboSet* bossSet = GetBossMeleeComboSet(weapon.type);
        if (bossSet != nullptr && HasUntriedStep(*bossSet, untriedSteps)) {
            gapTargets_.push_back({ name, true, false, name + " のボスの武器のコンボの段" });
        }
    }
    // とどめの型: 倒した近接武器に割り当てた型のうち、まだ試していない型ごとに1つ（ボスと向き合って、その武器で倒す）
    const std::set<std::string> untriedStyles = executions ? UntriedNames(kExecutionCategory) : std::set<std::string>();
    const auto& styleOfWeapon = BossExecutionTuningStore::Get().weapons;
    std::set<std::string> targetedStyles;
    for (const auto& weapon : list) {
        const std::string name = WeaponTypeName(weapon.type);
        const auto it = styleOfWeapon.find(name);
        if (it == styleOfWeapon.end() || untriedStyles.count(it->second) == 0 || !targetedStyles.insert(it->second).second) {
            continue;
        }
        gapTargets_.push_back({ name, false, true, "とどめの型 " + it->second + "（" + name + " でボスを倒す）" });
    }
}

/** @brief 今のステージのボスを置いた場所の手前へプレイヤーを移す（置いていなければ false） */
bool GameSmokeTest::TeleportToStageBoss() {
    const StageEditor* editor = StageEditor::GetActive();
    if (editor == nullptr) {
        return false;
    }
    bool found = false;
    VisitTarget target;
    editor->ForEachWorldObject([&found, &target](const ObjectDesc& desc, const Vector3& world) {
        if (!found && desc.isStageBoss) {
            target = { desc.name, desc.kind, world };
            found = true;
        }
    });
    if (found) {
        TeleportTo(target);
    }
    return found;
}

/** @brief coverGaps の手順を1フレーム進める（狙う物ごとに武器を渡して持ち替え、seconds の間だけ操作する。終わったら true） */
bool GameSmokeTest::UpdateCoverGaps(const SmokeTestStep& step) {
    if (runner_.GetStepFrame() == 0) {
        BuildGapTargets(step.raw.value("only", std::string()));
        gapIndex_ = 0;
        gapFrame_ = 0;
        sBossDuelRequested = false;
        runner_.ResetMonkeyState();
        if (gapTargets_.empty()) {
            runner_.AddLog("    狙う物は無い（まだ試していない物が無いか、武器を渡して起こせる物ではない）");
            return true;
        }
    }
    if (gapIndex_ >= gapTargets_.size()) {
        return true;
    }
    const GapTarget& target = gapTargets_[gapIndex_];
    if (gapFrame_ == 0) {
        // 武器を渡し、とどめを狙う時はボスと向き合う準備をゲーム側へ頼む
        runner_.AddLog("    狙う: " + target.reason);
        if (target.bossWeapon) {
            GrantBossWeapon(target.weapon, true);
            grantedSlot_ = WeaponManager::kBossSlot;
        } else {
            grantedSlot_ = GrantWeapon(target.weapon);
        }
        // ボスはまだ出ていないことがあるので、まず置いた場所へ近づく（出てきたらゲーム側が体力を1にして手前へ移す）
        if (target.duel && !TeleportToStageBoss()) {
            runner_.AddLog("    このステージにはボスが置かれていない");
        }
        sBossDuelRequested = target.duel;
    } else if (gapFrame_ < kSelectFrames) {
        PressSlotKey(runner_, grantedSlot_, gapFrame_);
    } else {
        // 操作は monkey と同じ（技の名前は手順の moves で絞る）。長さはこの手順が数えるので、monkey には終わらせない
        runner_.AdvanceMonkey(step);
    }
    ++gapFrame_;
    if (gapFrame_ >= kSelectFrames + step.frames) {
        // 次の物へ移る前に、押したままのキーとでたらめ操作の途中の状態を消す
        runner_.ReleaseKeys();
        runner_.ResetMonkeyState();
        ++gapIndex_;
        gapFrame_ = 0;
    }
    const bool done = gapIndex_ >= gapTargets_.size();
    if (done) {
        sBossDuelRequested = false;
    }
    return done;
}

} // namespace engine::game
