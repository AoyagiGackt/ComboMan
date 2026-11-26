/**
 * @file ScriptEffectReceiver.h
 * @brief ノードグラフから頼まれた画面の演出（文字・カメラ・操作停止）を受けて持つ部品を定義するファイル
 */
#pragma once
#include "Vector3.h"
#include <string>

namespace engine {
class Input;
}

namespace engine::game {

class FontRenderer;

/**
 * @brief ノードグラフの ShowMessage・FocusCamera・LockPlayerControl の受け口
 * @note Register() で SceneEffectBridge に受け口を登録し、Unregister() で外す。
 *       シーンは毎フレーム ApplyControlLock()・TakeCameraFocus()・DrawMessage() を呼ぶだけでよい。
 */
class ScriptEffectReceiver {
public:
    /**
     * @brief SceneEffectBridge へ受け口を登録する
     * @param input 操作を止める時に上書きする入力
     */
    void Register(Input* input);
    /** @brief 受け口を外し、操作を止めたままなら戻す（シーンを抜ける時） */
    void Unregister();
    /** @brief 操作を止めている間は、ポーズ以外の操作を押していない扱いにする（毎フレーム、入力を読む前に呼ぶ） */
    void ApplyControlLock();
    /**
     * @brief カメラを向ける先があれば取り出し、残り時間を1フレームぶん減らす
     * @param focus 向ける先（向ける先がある時だけ書く）
     * @return 向ける先があれば true
     */
    bool TakeCameraFocus(Vector3& focus);
    /**
     * @brief 出している文字を画面中央の下寄りに描き、残り時間を減らす
     * @param font 文字を描く先
     * @param layoutName UI 配置の名前
     */
    void DrawMessage(FontRenderer& font, const char* layoutName);

private:
    Input* input_ = nullptr; ///< 操作を止める時に上書きする入力（借りもの）
    std::wstring message_; ///< ShowMessage ノードが出した文字
    float messageSeconds_ = 0.0f; ///< その文字を出しておく残り秒数
    Vector3 cameraFocus_ = {}; ///< FocusCamera ノードが向けた先
    float cameraFocusSeconds_ = 0.0f; ///< カメラを向けておく残り秒数（0ならプレイヤーを追う）
    bool controlLocked_ = false; ///< LockPlayerControl ノードで操作を止めているか
};

} // namespace engine::game
