/**
 * @file Input.h
 * @brief DirectInput 8 を使用したキーボード入力管理を行うファイル
 */
#pragma once

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <XInput.h>
#include <dinput.h>

#include "WinApp.h"
#include <array>
#include <string>
#include <type_traits>
#include <vector>
#include <wrl/client.h>

#pragma comment(lib, "xinput.lib")
namespace engine {
/**
 * @brief キーボード入力を一括管理するクラス
 * @note DirectInput 8 を使用して、全256キーの状態を取得・管理します
 * 押しっぱなし判定（PushKey）と、押した瞬間判定（TriggerKey）を提供します
 */
class Input {
public: // メンバ関数
    /** @brief 操作に割り当てる左スティックの方向 */
    enum class StickDirection { kNone, kLeft, kRight, kUp, kDown };

    /** @brief しきい値に入力機器のデッドゾーンを使う指定 */
    static constexpr float kUseDeadzone = -1.0f;

    /** @brief 設定と操作案内で使う名前と既定の入力割り当て */
    struct ActionDef {
        const char* name; ///< 設定ファイル（input_bindings の actions）と案内の波括弧で使う操作名
        BYTE primaryKey; ///< 既定の主キー（0 は割り当て無し）
        BYTE secondaryKey; ///< 既定の副キー（0 は割り当て無し）
        WORD gamepadButton; ///< 既定のパッドのボタン（0 は割り当て無し）
        StickDirection stick; ///< 反応する左スティックの方向
        float stickThreshold; ///< スティックのしきい値（kUseDeadzone ならデッドゾーン）
    };

    /** @brief 操作番号順に割り当てを登録し、設定ファイルの内容を反映する */
    void SetActionTable(const std::vector<ActionDef>& defs);

    /** @brief 案内名の表記を返す関数。未対応の名前には空文字列を返す */
    using PromptResolver = std::wstring (*)(const Input&, const std::wstring&);

    /** @brief 追加の操作案内を解決する関数を登録する */
    void SetPromptResolver(PromptResolver resolver) {
        promptResolver_ = resolver;
    }

    /** @brief XInputが使っていないビットへ割り当てたトリガー（wButtonsと同じ扱いで押下判定できる） */
    static constexpr WORD kGamepadLeftTrigger = 0x0400;
    static constexpr WORD kGamepadRightTrigger = 0x0800;
    /** @brief DirectInput のキーボードで扱うキーの数 */
    static constexpr int kKeyCount = 256;
    /** @brief DirectInput のマウスで扱うボタンの数（DIMOUSESTATE2 の rgbButtons の要素数） */
    static constexpr int32_t kMouseButtonMax = static_cast<int32_t>(sizeof(DIMOUSESTATE2::rgbButtons));
    // namespace省略
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    /**
     * @brief 入力システムの初期化
     * @param winApp ウィンドウ管理クラスのポインタ
     * @note DirectInput インスタンスの生成、キーボードデバイスの初期化、データ形式の設定などを行います
     */
    void Initialize(WinApp* winApp);

    /**
     * @brief キー状態の更新
     * @note 毎フレーム呼び出すことで、現在のキー状態を前回の状態にコピーし、
     * デバイスから最新のキー状態を取得して更新します
     */
    void Update();

    /**
     * @brief キーが押されている（押しっぱなし）かチェック
     * @param keyNumber キーの番号（例: DIK_SPACE）
     * @return bool 押されていれば true
     */
    bool PushKey(BYTE keyNumber);

    /**
     * @brief キーが押された瞬間かチェック
     * @param keyNumber キーの番号（例: DIK_RETURN）
     * @return bool このフレームで押された瞬間なら true
     * @note 1フレーム前が離されていたかつ現在が押されている場合に true を返します
     */
    bool TriggerKey(BYTE keyNumber);

    /** @brief 毎フレームの更新（コントローラー用） */
    void UpdateGamepad();

    /** @brief ボタンが押されているか */
    bool PushButton(WORD button) const {
        return primaryGamepadEnabled_ && (state_.Gamepad.wButtons & button);
    }

    /** @brief ボタンが押された瞬間か */
    bool TriggerButton(WORD button) const {
        return primaryGamepadEnabled_ && (state_.Gamepad.wButtons & button) && !(previousState_.Gamepad.wButtons & button);
    }

    /** @brief 左スティックの値を 0.0 ~ 1.0 の範囲で取得 */
    struct Stick {
        float x, y;
    };

    /** @brief 指定したパッドのボタンが押されているかを返す */
    bool PadPushed(int pad, WORD button) const;
    /** @brief 指定したパッドのボタンが押された瞬間かを返す */
    bool PadTriggered(int pad, WORD button) const;
    /** @brief 指定したパッドの左スティックを返す */
    Stick PadLeftStick(int pad) const;
    /** @brief 指定したパッドの接続状態を返す */
    bool IsPadConnected(int pad) const;
    /** @brief 主操作でパッドを使うかを設定する */
    void SetPrimaryGamepadEnabled(bool enabled) {
        primaryGamepadEnabled_ = enabled;
    }
    /** @brief 主操作でキーボードを使うかを設定する（false の間は操作の読み取りでキーボードを見ない。メニュー操作と直接のキー読み取りは外さない） */
    void SetPrimaryKeyboardEnabled(bool enabled) {
        primaryKeyboardEnabled_ = enabled;
    }

    Stick GetLeftStick() const;

    /** @brief 指定アクションが押されているかを返す */
    bool PushAction(int action) const;

    /** @brief 列挙値で指定した操作が押されているかを返す */
    template <class E> requires std::is_enum_v<E>
    bool PushAction(E action) const {
        return PushAction(static_cast<int>(action));
    }

    /** @brief 指定アクションが押された瞬間かを返す */
    bool TriggerAction(int action) const;

    /** @brief 列挙値で指定した操作が押された瞬間かを返す */
    template <class E> requires std::is_enum_v<E>
    bool TriggerAction(E action) const {
        return TriggerAction(static_cast<int>(action));
    }

    /**
     * @brief 指定アクションの押下状態を実機入力の代わりに強制する（タイトルデモ等の自動操作用）
     * @note 呼んだアクションだけ実機入力を無視するようになる（PushKey/TriggerKey等の生キー参照や、
     * オーバーライドしていない他のActionには一切影響しない）。毎フレーム希望する状態で呼び直すこと
     * （1フレームだけtrueにしてTriggerAction()へパルスを起こしたい場合、次フレームは明示的にfalseで呼ぶ）
     */
    void SetActionOverride(int action, bool pressed);

    /** @brief 列挙値で指定した操作の押下状態を強制する */
    template <class E> requires std::is_enum_v<E>
    void SetActionOverride(E action, bool pressed) {
        SetActionOverride(static_cast<int>(action), pressed);
    }

    /** @brief 全アクションのオーバーライドを解除し、実機入力の参照に戻す */
    void ClearActionOverrides();

    /**
     * @brief 自動テスト用に、キーを押したことにする（実機の入力に重ねる。次の Update() から効く）
     * @note 押したままにするなら毎フレーム呼ばなくてよい。離す時は pressed=false か ClearInjectedKeys()
     */
    void InjectKey(BYTE keyNumber, bool pressed);

    /** @brief 押したことにしていたキーをすべて離す */
    void ClearInjectedKeys();

    /**
     * @brief 本物のキーボード・マウス・パッドの入力を使うかを決める（自動テストで同じ動きを再現する時は切る）
     * @param enabled false なら本物の入力を押していない扱いにする（Esc だけはアプリを閉じられるよう通す）
     */
    void SetPhysicalInputEnabled(bool enabled) {
        physicalInputEnabled_ = enabled;
    }
    /** @brief 本物の入力を使っているか */
    bool IsPhysicalInputEnabled() const {
        return physicalInputEnabled_;
    }

    /** @brief キーの名前（"Enter" "F5" "Space" など、input_bindings.json と同じ書き方）から DIK のキー番号を返す（知らない名前なら 0） */
    static BYTE KeyFromName(const std::string& name);

    /** @brief ゲームパッドが現在接続されているかを返す */
    bool IsGamepadConnected() const {
        return gamepadConnected_;
    }

    /** @brief このフレームでゲームパッドが接続されたかを返す */
    bool WasGamepadConnected() const {
        return gamepadConnected_ && !gamepadConnectedPrevious_;
    }

    /** @brief このフレームでゲームパッドが切断されたかを返す */
    bool WasGamepadDisconnected() const {
        return !gamepadConnected_ && gamepadConnectedPrevious_;
    }

    /** @brief 最後に操作した機器がゲームパッドか（操作説明の表記切り替えに使う） */
    bool IsUsingGamepad() const {
        return usingGamepad_;
    }

    // メニュー操作（キーボード・十字キー・左スティックのどれでも反応する）
    bool TriggerMenuUp() const;
    bool TriggerMenuDown() const;
    bool TriggerMenuLeft() const;
    bool TriggerMenuRight() const;
    /** @brief 決定（Space/Enter/Aボタン） */
    bool TriggerMenuConfirm() const;
    /** @brief 戻る（Backspace/Bボタン。Escはアプリ終了なので含めない） */
    bool TriggerMenuCancel() const;

    /** @brief 操作の表示名を、最後に使った機器に合わせて返す（例: キーボードなら "J"、パッドなら "LT"） */
    std::wstring GetActionLabel(int action) const;

    /** @brief 列挙値で指定した操作の表示名を返す */
    template <class E> requires std::is_enum_v<E>
    std::wstring GetActionLabel(E action) const {
        return GetActionLabel(static_cast<int>(action));
    }

    /**
     * @brief 文章中の {操作名} を、最後に使った機器のボタン名へ置き換える
     * @param text 登録した操作名や案内名を波括弧で囲んだ文章
     * @return 置き換え後の文章（知らない名前はそのまま残す）
     */
    std::wstring ExpandPrompts(const std::wstring& text) const;

    /** @brief 最後に初期化した入力（シーンから入力を渡されないHUD等が操作説明を作るために使う） */
    static const Input* GetCurrent() {
        return sCurrent;
    }

    // マウス関連
    bool TriggerMouseButton(int32_t buttonNumber);
    /** @brief マウスボタンが押されている間trueを返す */
    bool PushMouseButton(int32_t buttonNumber) const {
        return buttonNumber >= 0 && buttonNumber < kMouseButtonMax && (mouseState_.rgbButtons[buttonNumber] & 0x80) != 0;
    }
    /** @brief クライアント領域を基準にしたマウス座標を取得する */
    POINT GetMouseClientPosition() const {
        POINT point = {};
        GetCursorPos(&point);
        if (winApp_) {
            ScreenToClient(winApp_->GetHwnd(), &point);
        }
        return point;
    }

    /** @brief マウスのホイールスクロール量を取得する */
    int32_t GetWheel() const {
        return mouseState_.lZ;
    }

private:
    /** @brief ゲーム内アクションに対応するキーボードとパッドの割り当て */
    struct ActionBinding {
        std::wstring name; ///< 設定ファイルと案内の波括弧で使う操作名
        BYTE primaryKey; ///< 主キー（0 は割り当て無し）
        BYTE secondaryKey; ///< 副キー（0 は割り当て無し）
        WORD gamepadButton; ///< パッドのボタン（0 は割り当て無し）
        StickDirection stick; ///< 反応する左スティックの方向
        float stickThreshold; ///< スティックのしきい値（kUseDeadzone ならデッドゾーン）
    };

    /** @brief DirectInput 8 の本体ポインタ */
    ComPtr<IDirectInput8> directInput_;

    /** @brief キーボードデバイスのポインタ */
    ComPtr<IDirectInputDevice8> keyboard_;

    // キー状態管理用バッファ

    /** @brief 最新のキー状態（kKeyCount 個のキー分） */
    BYTE key_[kKeyCount] = { };
    /** @brief 自動テストが押したことにしているキー（InjectKey 参照） */
    std::array<bool, kKeyCount> injectedKeys_ { };
    bool physicalInputEnabled_ = true; ///< 本物のキーボード・マウス・パッドの入力を使うか（SetPhysicalInputEnabled 参照）

    /** @brief 1フレーム前のキー状態（kKeyCount 個のキー分） */
    BYTE keyPre_[kKeyCount] = { };

    /** @brief ウィンドウ管理のポインタ */
    WinApp* winApp_ = nullptr;

    // コントローラー状態管理用

    XINPUT_STATE state_ { }; /// 現在のコントローラー状態
    XINPUT_STATE previousState_ { }; /// 前回のコントローラー状態
    XINPUT_STATE secondPadState_ { }; ///< ２台目の現在の状態
    XINPUT_STATE secondPadPreviousState_ { }; ///< ２台目の前回の状態
    bool secondPadConnected_ = false; ///< ２台目の接続状態
    bool primaryGamepadEnabled_ = true; ///< 主操作でパッドを使うか
    bool primaryKeyboardEnabled_ = true; ///< 主操作でキーボードを使うか
    const float deadzone_ = 0.2f; /// デッドゾーン
    bool gamepadConnected_ = false; ///< 現在の接続状態
    bool gamepadConnectedPrevious_ = false; ///< 前フレームの接続状態
    bool usingGamepad_ = false; ///< 最後に操作した機器がゲームパッドか
    std::vector<ActionBinding> actionBindings_;
    PromptResolver promptResolver_ = nullptr;

    static inline const Input* sCurrent = nullptr;

    /** @brief 左スティックが指定方向へこのフレームに倒されたか（倒しっぱなしでは連続しない） */
    bool TriggerStick(int axisX, int axisY) const;
    /** @brief このフレームの入力から、最後に使った機器を更新する */
    void UpdateLastDevice();
    /** @brief このフレームに押されたキー・マウス・パッドのボタンを、落ちた時の調査用に直前の出来事へ記録する */
    void RecordInputBreadcrumbs() const;

    // アクションオーバーライド（SetActionOverride参照）。有効なアクションだけ実機入力より優先する
    std::vector<bool> actionOverrideEnabled_;
    std::vector<bool> actionOverridePressed_;
    std::vector<bool> actionOverridePressedPrev_;

    // マウス状態管理用

    /** @brief マウスデバイスのポインタ */
    Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;
    /** @brief マウスの現在の状態 */
    DIMOUSESTATE2 mouseState_ = { };
    /** @brief マウスの前回の状態 */
    DIMOUSESTATE2 mouseStatePre_ = { };
};

} // namespace engine
