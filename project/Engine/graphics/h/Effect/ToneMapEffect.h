/**
 * @file ToneMapEffect.h
 * @brief 1 を超える明るさを持つシーンの色を、画面に出せる 0〜1 へ丸めるトーンマップを定義するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include <cstdint>
#include <wrl/client.h>

namespace engine::graphics {

/**
 * @brief 描き終えた 3D の絵を写し取り、明るすぎる所をなめらかに丸めて描き戻すトーンマップのシングルトンクラス
 * @note シーンは浮動小数点の色バッファへ描くので、加算で重ねた光は 1 を超えたまま残っている。
 *       3D とブルームの後・UI の前に CaptureAndApply を呼ぶと、UI の色は変えずに 3D の光だけを丸められる。
 *       呼ばない場面では、フレームの最後にスワップチェーンへ写す時に 1 で切り捨てる（今までと同じ見た目）。
 *       丸め方は、肩だけ丸める曲線（暗い所と中間はそのまま）と、映画風の曲線を混ぜる割合で選ぶ
 */
class ToneMapEffect {
public:
    /** @brief 共有インスタンスを取得する */
    static ToneMapEffect* GetInstance() {
        static ToneMapEffect sInstance;
        return &sInstance;
    }

    /**
     * @brief 写し取り先・パイプライン・定数を作り、graphics_tuning.json の tonemap の値を読む
     * @param dxCommon DirectX共通基盤
     * @param srvManager SRVディスクリプタヒープ管理
     */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);
    /** @brief 作った資源を解放する */
    void Finalize();

    /**
     * @brief 今の描画先のゲームの絵の範囲を写し取り、トーンマップを掛けて描き戻す
     * @note 描画先とパイプラインを書き換えるので、呼んだ後は描画先と共通の描画設定を元に戻すこと
     */
    void CaptureAndApply();

    /** @brief トーンマップを使うかを設定する */
    void SetEnabled(bool enabled) {
        enabled_ = enabled;
    }
    /** @brief トーンマップを使うかを返す */
    bool IsEnabled() const {
        return enabled_;
    }

private:
    /** @brief TonemapPS.hlsl の TonemapParams と同じ並びの定数 */
    struct Parameters {
        float exposure = 1.0f; ///< 丸める前に掛ける明るさの倍率
        float filmicBlend = 0.0f; ///< 映画風の曲線を混ぜる割合（0 で肩だけ丸める曲線、1 で映画風の曲線）
        float shoulderStart = 0.8f; ///< 肩だけ丸める曲線で、丸め始める明るさ（これ以下はそのまま）
        float padding = 0.0f; ///< 16 バイトにそろえる詰め物
    };

    /** @brief 資源が未確保の状態を作る */
    ToneMapEffect() = default;
    /** @brief 所有する資源はメンバのデストラクタで解放する */
    ~ToneMapEffect() = default;
    ToneMapEffect(const ToneMapEffect&) = delete;
    ToneMapEffect& operator=(const ToneMapEffect&) = delete;

    engine::DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constants_; ///< Parameters を置く定数バッファ（初期化の時に1回だけ書く）
    Microsoft::WRL::ComPtr<ID3D12Resource> captureTexture_; ///< 描き終えた絵を写し取る先
    uint32_t captureSrvIndex_ = UINT32_MAX;
    bool enabled_ = true;
};

} // namespace engine::graphics
