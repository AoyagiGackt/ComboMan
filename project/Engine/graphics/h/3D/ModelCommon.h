/**
 * @file ModelCommon.h
 * @brief 3Dモデルを描画するための共通グラフィックスパイプライン（PSO）やルートシグネチャを管理するファイル
 */
#pragma once
#include "BlendMode.h"
#include "DirectXCommon.h"
#include <atomic>
#include <wrl/client.h>
namespace engine::graphics {

/**
 * @brief 3Dモデル描画の共通設定を保持するクラス
 */
class ModelCommon {
public:
    /**
     * @brief 共通描画設定の初期化ルートシグネチャとPSOを生成する
     */
    void Initialize(engine::DirectXCommon* dxCommon);

    /**
     * @brief 描画コマンドにこの共通設定をセットする
     * @param blendMode 使用するブレンドモード
     */
    void CommonDrawSettings(BlendMode blendMode = BlendMode::kAlpha);

    /**
     * @brief まとめ描き用のパイプラインに切り替える（直前の CommonDrawSettings と同じブレンドモード）
     * @note ルートシグネチャは同じなので、設定済みのライト・マテリアルなどのルート引数はそのまま残る
     */
    void BeginInstancedDraw();

    /** @brief まとめ描きの後、通常のパイプラインに戻す */
    void EndInstancedDraw();

    /**
     * @brief シャドウパス開始（シャドウ用 PSO + Root Signature をセット）
     */
    void BeginShadowPass();

    /**
     * @brief シャドウパス終了（通常 PSO への切り替えは次の CommonDrawSettings() で行う）
     */
    void EndShadowPass();

    /** @brief DirectX の共通処理を返す */
    engine::DirectXCommon* GetDxCommon() const {
        return dxCommon_;
    }

private:
    engine::DirectXCommon* dxCommon_ = nullptr;

    // 通常描画用
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStates_[static_cast<size_t>(BlendMode::kCount)];
    /** @brief まとめ描き用 PSO（ブレンドモード別。1体ごとの行列を頂点バッファの2本目から読む） */
    Microsoft::WRL::ComPtr<ID3D12PipelineState> instancedPipelineStates_[static_cast<size_t>(BlendMode::kCount)];
    /** @brief 直前の CommonDrawSettings で選んだブレンドモード（バンドルを別のスレッドで記録するので atomic） */
    std::atomic<BlendMode> currentBlendMode_ = BlendMode::kAlpha;

    // シャドウパス用
    /** @brief シャドウパス専用ルートシグネチャ（CBV 1つ TransformationMatrix） */
    Microsoft::WRL::ComPtr<ID3D12RootSignature> shadowRootSignature_;
    /** @brief シャドウパス専用 PSO（深度のみ書き込み） */
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPipelineState_;
};

} // namespace engine::graphics
