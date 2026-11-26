/**
 * @file ToneMapEffect.cpp
 * @brief ToneMapEffect の写し取りとトーンマップの描き戻しを実装するファイル
 */
#include "ToneMapEffect.h"
#include "EngineAssert.h"
#include "GraphicsTuning.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"

namespace engine::graphics {
namespace {
/** @brief 露出の倍率の上限（調整値の打ち間違いで真っ白にならないようにする） */
constexpr float kMaxExposure = 16.0f;
/** @brief 丸め始める明るさの上限（1 にすると丸める幅が無くなるので、それより少し下で止める） */
constexpr float kMaxShoulderStart = 0.99f;
} // namespace

/** @brief 写し取り先・パイプライン・定数を作り、graphics_tuning.json の tonemap の値を読む */
void ToneMapEffect::Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager) {
    ENGINE_ASSERT(dxCommon);
    ENGINE_ASSERT(srvManager);
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;

    // 調整値は起動の時に1回だけ読み、定数バッファへ書いたままにする（毎フレームは書かない）
    Parameters parameters;
    parameters.exposure = GraphicsTuning::Float("tonemap.exposure", parameters.exposure, 0.0f, kMaxExposure);
    parameters.filmicBlend = GraphicsTuning::Float("tonemap.filmicBlend", parameters.filmicBlend, 0.0f, 1.0f);
    parameters.shoulderStart = GraphicsTuning::Float("tonemap.shoulderStart", parameters.shoulderStart, 0.0f, kMaxShoulderStart);
    enabled_ = GraphicsTuning::Int("tonemap.enabled", 1, 0, 1) != 0;

    constants_ = dxCommon_->CreateBufferResource(AlignConstantBufferSize(sizeof(Parameters)));
    Parameters* mapped = nullptr;
    constants_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    *mapped = parameters;
    constants_->Unmap(0, nullptr);

    // 写し取り先（シーンの色バッファと同じ形式）と、全画面で描き戻すパイプライン
    captureTexture_ = pipelineHelper::CreateBackBufferCopyTexture(dxCommon_, srvManager_, captureSrvIndex_);
    rootSignature_ = pipelineHelper::CreateCapturedFullscreenSignature(dxCommon_->GetDevice());
    ENGINE_ASSERT(rootSignature_);
    pipelineState_ = pipelineHelper::CreateFullscreenPipeline(dxCommon_, rootSignature_.Get(),
        L"Resources/shaders/postprocess/TonemapPS.hlsl", BlendMode::kNone);
}

/** @brief 作った資源を解放する */
void ToneMapEffect::Finalize() {
    pipelineState_.Reset();
    rootSignature_.Reset();
    constants_.Reset();
    captureTexture_.Reset();
}

/** @brief 今の描画先のゲームの絵の範囲を写し取り、トーンマップを掛けて描き戻す */
void ToneMapEffect::CaptureAndApply() {
    pipelineHelper::CaptureAndDrawFullscreen(enabled_, dxCommon_, srvManager_, captureTexture_,
        rootSignature_.Get(), pipelineState_.Get(), constants_.Get(), captureSrvIndex_);
}

} // namespace engine::graphics
