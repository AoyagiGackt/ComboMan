/**
 * @file WeaponManager.h
 * @brief スタイル別の武器データを管理するシングルトン
 */
#pragma once
#include "Weapon.h"
#include <array>
#include <vector>
namespace engine::game {

/// @brief スタイル別近接武器と射撃武器の定義を保持し、選択中スタイルを管理する
class WeaponManager {
public:
    /** @brief 同時に持てる近接武器の数（HUD・数字キー・交換画面もこの数に追従する） */
    static constexpr int kSlotCount = 3;
    /** @brief ボスから奪った武器の専用の枠の番号（ふつうの枠の後ろ。選んでいる枠の番号としてだけ使う） */
    static constexpr int kBossSlot = kSlotCount;

    /** @brief ボスの武器を手に入れた結果 */
    enum class BossAcquireResult {
        kAdded, ///< ボスの枠が空いていたので入れた
        kSame, ///< ボスの枠の武器か、その相棒と同じ種類だった（そのまま）
        kFusedSame, ///< 合体していないボスの枠の武器と同じ種類だったので、同じ種類どうしで合体した
        kNeedsMainChoice, ///< 別の種類だったので、合体の主軸をどちらにするか選ぶ
    };

    /** @brief 武器入手をスロットへ反映した結果 */
    enum class AcquireResult {
        kAdded,
        kDuplicate,
        kNeedsReplacement,
    };

    /// @brief シングルトンインスタンスを取得する
    static WeaponManager* GetInstance();

    /**
     * @brief 画面に出す武器名を返す（剣・槍などの武器の種類の名前）
     * @param data 名前を出す武器
     */
    static std::wstring GetDisplayName(const WeaponData& data);

    /// @brief 選択中の近接武器データを返す
    const WeaponData& GetCurrent() const {
        return weapons_[index_];
    }
    /// @brief 選択中の射撃武器データを返す
    const RangedWeaponData& GetRanged() const {
        return rangedWeapons_[rangedIndex_];
    }
    /// @brief 選択中スタイルのインデックスを返す（0始まり）
    int GetIndex() const {
        return index_;
    }
    /// @brief スタイル総数を返す
    int GetCount() const {
        return static_cast<int>(weapons_.size());
    }
    /// @brief 全スタイルのリストを返す
    const std::vector<WeaponData>& GetList() const {
        return weapons_;
    }

    /// @brief 次の銃へ切り替える（循環。銃は近接と違い最初から全部使える）
    void SelectNextRanged() {
        rangedIndex_ = (rangedIndex_ + 1) % static_cast<int>(rangedWeapons_.size());
    }

    /// @brief 次の解放済みスタイルへ切り替える（循環、未解放はスキップ）
    void SelectNext();
    /// @brief 前の解放済みスタイルへ切り替える（循環、未解放はスキップ）
    void SelectPrev();

    /** @brief 全解放済み武器を対象に次を選び、現在スロットへ割り当てる */
    void SelectNextUnlockedInCurrentSlot();

    /** @brief 全解放済み武器を対象に前を選び、現在スロットへ割り当てる */
    void SelectPrevUnlockedInCurrentSlot();

    /// @brief 指定インデックスのスタイルが解放済みか
    bool IsUnlocked(int i) const {
        return i >= 0 && i < static_cast<int>(unlocked_.size()) && unlocked_[i];
    }

    /** @brief 指定したスロットを選択する */
    void SelectSlot(int slot);
    /** @brief スロットに登録された武器インデックスを返す */
    int GetSlotWeaponIndex(int slot) const;
    /** @brief その種類の武器をどれかのスロットに持っているか */
    bool Owns(WeaponType type) const;
    /** @brief 現在選択しているスロット番号を返す */
    int GetSelectedSlot() const {
        return selectedSlot_;
    }
    /** @brief 武器を装備しているかを返す */
    bool HasEquippedWeapon() const {
        return GetSlotWeaponIndex(selectedSlot_) >= 0;
    }
    /**
     * @brief 武器を空きスロットへ追加し、満杯なら交換待ちにする
     * @note 戻り値が Duplicate（所持済みタイプの再入手）の場合、呼び出し側（GamePlayScene）が
     * player_->ChargeAwakenGauge(CombatTuning::duplicateWeaponAwakenBonus) で覚醒ゲージへ転用する
     */
    AcquireResult Acquire(WeaponType type);
    /** @brief 交換待ちの武器で指定スロットを置き換える */
    void ReplacePendingWeapon(int slot);
    /** @brief 交換待ちの武器を破棄する */
    void DiscardPendingWeapon();
    /** @brief 武器交換の選択待ちか返す */
    bool HasPendingWeapon() const {
        return pendingWeaponIndex_ >= 0;
    }
    /** @brief 交換待ちの武器データを返す */
    const WeaponData& GetPendingWeapon() const {
        return weapons_[pendingWeaponIndex_];
    }

    /**
     * @brief 道中で奪った武器を合体させるふつうの枠を選ぶ（選んでいる枠がふつうの枠ならそこ、ボスの枠なら合体していないふつうの枠）
     * @return 合体させる枠（ふつうの枠が空なら -1）
     */
    int FindFusionSlot() const;
    /**
     * @brief 交換待ちの武器を、指定した枠の武器に合体させる（合体した武器は強いが、耐久が尽きると枠ごと砕ける）
     * @param slot 合体させる枠
     * @param durability 合体の耐久（当てるたびに1減る）
     * @return 合体できたら true（枠が空なら合体せず、交換待ちはそのまま残す）
     */
    bool FusePendingInto(int slot, int durability);
    /**
     * @brief 指定した種類の武器を、指定した枠の武器に合体させる（もう持っている種類を奪った時に使う）
     * @param slot 合体させる枠
     * @param type 合体させる武器の種類
     * @param durability 合体の耐久（当てるたびに1減る）
     * @return 合体できたら true（枠が空・その種類の武器が無い時は false）
     */
    bool FuseInto(int slot, WeaponType type, int durability);
    /**
     * @brief ボスから奪った武器をボスの枠へ入れる（ボスの枠の武器は入れ替わらず、合体しても砕けない）
     * @param type ボスから奪った武器の種類
     * @param allowFusion 合体してよいか（設定で合体を切っている時は false。同じ種類でも合体せず kSame になる）
     * @return 入れたか・同じだったか・同じ種類どうしで合体したか・主軸を選ぶ必要があるか
     */
    BossAcquireResult AcquireBossWeapon(WeaponType type, bool allowFusion = true);
    /** @brief ボスの枠に武器が入っているか */
    bool HasBossWeapon() const {
        return bossWeaponIndex_ >= 0;
    }
    /** @brief ボスの枠を選んでいるか */
    bool IsBossSlotSelected() const {
        return selectedSlot_ == kBossSlot && bossWeaponIndex_ >= 0;
    }
    /** @brief 合体の主軸を選ぶ待ちか（別の種類のボスの武器を手に入れた直後） */
    bool HasPendingBossChoice() const {
        return pendingBossIndex_ >= 0;
    }
    /** @brief 主軸を選ぶ待ちの、新しく手に入れたボスの武器 */
    const WeaponData& GetPendingBossWeapon() const {
        return weapons_[pendingBossIndex_];
    }
    /**
     * @brief 合体の主軸を決める（選ばなかった方が相棒になり、前の相棒は外れる）
     * @param keepCurrentMain true なら今の主軸のまま、false なら新しいボスの武器を主軸にする
     */
    void ChooseBossMain(bool keepCurrentMain);
    /** @brief 主軸を選ぶ待ちの新しいボスの武器を手放す（合体を切っている時。今のボスの武器はそのまま） */
    void DiscardPendingBossWeapon() {
        pendingBossIndex_ = -1;
    }
    /** @brief すべての枠に武器が入っているか */
    bool AreSlotsFull() const;
    /** @brief 指定した枠の武器に合体している相棒の武器の番号を返す（合体していなければ -1） */
    int GetFusedWeaponIndex(int slot) const;
    /** @brief 指定した枠の合体の残り耐久の割合を返す（0〜1、合体していなければ 0） */
    float GetFusionDurabilityRatio(int slot) const;
    /** @brief 選んでいる枠の武器が合体しているか */
    bool IsCurrentFused() const {
        return GetFusedWeaponIndex(selectedSlot_) >= 0;
    }
    /** @brief 選んでいる枠の武器に合体している相棒の武器（合体していなければ nullptr） */
    const WeaponData* GetCurrentFusionPartner() const;
    /**
     * @brief 選んでいる枠の合体の耐久を減らす。尽きたら枠の武器ごと砕き、ほかの枠の武器へ持ち替える
     * @param amount 減らす量
     * @return 砕けたら true
     */
    bool ConsumeFusionDurability(int amount);

    /**
     * @brief 指定タイプの武器を解放し、そのまま装備する（敵からの武器奪取用）
     * @return 新規解放なら true既に解放済み（重複入手）なら false を返す
     * @note 実際の武器奪取フローは Acquire() を直接使う（Duplicate/NeedsReplacement を区別できるため）。
     * この関数はAddedかどうかだけを見たい単純なケース向けの薄いラッパー
     */
    bool Unlock(WeaponType type);

    /** @brief 全武器を解放し、先頭からスロット数ぶんを登録する */
    void UnlockAll();

    /**
     * @brief トレーニング用に指定武器を現在のスロットへ装備する
     * @param type 装備する武器種別
     */
    void EquipForTraining(WeaponType type);

    /**
     * @brief タイトルデモ用に指定武器を装備する（スロットから押し出した武器も解放済みのまま残す）
     * @param type 装備する武器種別
     */
    void EquipForDemo(WeaponType type);

    /**
     * @brief 全武器を未解放状態へ戻す（新しいランの開始時に呼ぶ）
     * @note シングルトンのためトレーニング等で装備した武器がシーンをまたいで残ってしまうのを防ぐ
     */
    void Reset();

    /**
     * @brief 新しいランの開始時に呼ぶ。Reset()した上でgame_rules.jsonのstarterWeaponを最初のスロットへ装備する
     * @note starterWeaponが空文字なら素手のまま開始する
     */
    void ResetForNewRun();

    /** @brief 1枠ぶんの合体の状態（土台の武器が入れ替わったら無効として扱う） */
    struct Fusion {
        int baseIndex = -1; ///< 合体した時に枠に入っていた武器の番号
        int partnerIndex = -1; ///< 合体させた相棒の武器の番号（-1 なら合体していない）
        int durability = 0; ///< 残りの耐久
        int maxDurability = 0; ///< 合体した時の耐久
    };
    /** @brief 解放状況・装備状態の実行時スナップショット（タイトルデモ等、一時的にUnlockAll()する場面の退避・復元用） */
    struct Snapshot {
        std::vector<bool> unlocked;
        std::array<int, kSlotCount> slots { -1, -1, -1 };
        int index = 0;
        int selectedSlot = -1;
        int pendingWeaponIndex = -1;
        int rangedIndex = 0;
        std::array<Fusion, kSlotCount> fusions {}; ///< 枠ごとの合体（セーブには残さない）
        int bossWeapon = -1; ///< ボスの枠の武器の番号（-1 なら空）
        int bossPartner = -1; ///< ボスの枠に合体している相棒の番号（-1 なら合体していない）
    };
    /** @brief 現在の解放状況・装備状態を退避する */
    Snapshot SaveSnapshot() const;
    /** @brief SaveSnapshot()で退避した状態へ戻す */
    void RestoreSnapshot(const Snapshot& snapshot);

private:
    WeaponManager();
    /** @brief 解放済みの武器を direction（+1 で次、-1 で前）の向きに探し、現在のスロットへ割り当てる */
    void SelectUnlockedInCurrentSlot(int direction);

    std::vector<WeaponData> weapons_;
    std::vector<bool> unlocked_;
    std::array<int, kSlotCount> slots_ { -1, -1, -1 };
    /** @brief 指定した枠の合体の状態が今も有効か（枠の武器が合体した時のままか） */
    bool IsFusionValid(int slot) const;
    /** @brief すべての枠の合体を解く */
    void ClearFusions();
    std::array<Fusion, kSlotCount> fusions_ {};
    int bossWeaponIndex_ = -1; ///< ボスの枠の武器の番号（-1 なら空。ふつうの枠の武器とは別に持ち、unlocked_ には数えない）
    int bossPartnerIndex_ = -1; ///< ボスの枠に合体している相棒の番号（-1 なら合体していない。耐久は無い）
    int pendingBossIndex_ = -1; ///< 主軸を選ぶ待ちの新しいボスの武器の番号
    /** @brief 指定した枠に武器が入っているか（ボスの枠も含む） */
    bool IsSlotFilled(int slot) const {
        return GetSlotWeaponIndex(slot) >= 0;
    }
    /** @brief 指定した枠の武器に相棒を合体させる（耐久を満タンにする） */
    void SetFusion(int slot, int partnerIndex, int durability);
    std::vector<RangedWeaponData> rangedWeapons_;
    int index_ = 0;
    int selectedSlot_ = -1;
    int pendingWeaponIndex_ = -1;
    int rangedIndex_ = 0;
};

} // namespace engine::game
