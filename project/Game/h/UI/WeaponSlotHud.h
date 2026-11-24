/**
 * @file WeaponSlotHud.h
 * @brief 武器枠と所持武器アイコンの表示を担当するHUD要素を定義するファイル
 */
#pragma once
#include "HudLayer.h"
#include "Object3d.h"
#include "Sprite.h"
#include "WeaponManager.h"
#include <array>

namespace engine::game {
/** @brief 武器枠・実物アイコン・選択演出をまとめて管理する。 */
class WeaponSlotHud final : public IHudWidget {
public:
    void Initialize(const HudServices& services) override;
    void Update(const HudFrame& frame) override;
    void QueueText(FontRenderer& font) const override;
    void Draw() override;
    void Notify(HudEvent event) override;
private:
    static constexpr int kSlotCount = WeaponManager::kSlotCount;
    static constexpr float kSlotSize = 56.0f;
    static constexpr float kFlashDuration = 0.35f;
    /** @brief プールまたはHUDの1枠が保持する表示資源と状態 */
    struct Slot {
        std::unique_ptr<engine::graphics::Sprite> frame;
        std::unique_ptr<engine::graphics::Sprite> icon;
        std::unique_ptr<engine::graphics::Sprite> durabilityBack; ///< 合体の耐久のバーの下地
        std::unique_ptr<engine::graphics::Sprite> durabilityFill; ///< 合体の耐久のバーの残り
        bool fused = false; ///< このフレームに合体しているか（バーを描くか）
        int modelIcon = -1; ///< このフレームに出すモデルアイコンの武器の番号（モデルで出さない時は -1）
        int partnerIcon = -1; ///< 合体の素材のモデルアイコンの番号（出さない時は -1）
        Vector2 position = {};
    };
    /**
     * @brief 合体している枠の枠を相棒の色で光らせ、耐久のバーを残りに合わせて伸ばす
     * @param slot   表示する枠
     * @param slotIndex 枠の番号
     */
    void UpdateFusionDisplay(Slot& slot, int slotIndex);
    /** @brief HUDに武器モデルを表示するための描画資源と姿勢補正 */
    struct ModelIcon {
        std::unique_ptr<engine::graphics::Object3d> object;
        float scale = 0.2f;
        float baseYaw = 0.0f;
    };
    void InitializeFrames();
    /** @brief UILayoutから枠の位置・間隔を読み、枠とアイコンを並べ直す（エディタで動かした結果を毎フレーム反映する） */
    void ApplyLayout();
    /** @brief その枠でその武器を3Dモデルのアイコンで出せるか */
    bool HasModelIcon(int slotIndex, int weaponIndex) const;

    /** @brief 1枠ぶんのアイコンを描き分けるのに使う、そのフレームの状態 */
    struct SlotIconState {
        int slotIndex = 0; ///< 枠の番号（ボスの枠を含む）
        int weaponIndex = -1;
        bool active = false; ///< 選択中の枠か
        bool unlocked = false; ///< 武器を手に入れているか
        float pulse = 0.0f; ///< 選択中の明滅
        float flash = 0.0f; ///< 武器を手に入れた直後の光り（0〜1）
        int partnerIndex = -1; ///< 合体している素材の武器の番号（合体していなければ -1）
        Vector3 cameraPosition {};
    };
    /** @brief 武器の3Dモデルを枠の位置に浮かべる */
    void ShowModelIcon(Slot& slot, const SlotIconState& state);
    /** @brief 武器の色で塗ったアイコンを出す */
    void ShowColorIcon(Slot& slot, const SlotIconState& state);
    /** @brief まだ手に入れていない枠を暗く出す */
    void ShowLockedIcon(Slot& slot, const SlotIconState& state);
    engine::graphics::SpriteCommon* sprites_ = nullptr;
    engine::graphics::ModelCommon* models_ = nullptr;
    engine::DirectXCommon* graphics_ = nullptr;
    WeaponManager* weapons_ = nullptr;
    std::array<Slot, kSlotCount + 1> slots_;
    // 枠ごとに武器の数だけモデルのアイコンを持つ（ふつうの枠とボスの枠に同じ種類が入っても、それぞれの位置に出せるように）
    std::array<std::vector<ModelIcon>, kSlotCount + 1> modelIcons_;
    std::array<std::vector<ModelIcon>, kSlotCount + 1> partnerIcons_; ///< 合体の素材として土台のアイコンに付けるモデル（土台と同じ種類でも描けるよう別に持つ）
    std::unique_ptr<engine::graphics::Sprite> gunFrame_;
    std::unique_ptr<engine::graphics::Sprite> gunIcon_;
    Vector2 gunPosition_ = {};
    float pulseTime_ = 0.0f;
    float flashRemaining_ = 0.0f;
};
}
