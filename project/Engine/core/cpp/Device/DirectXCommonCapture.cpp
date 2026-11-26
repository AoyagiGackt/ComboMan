/**
 * @file DirectXCommonCapture.cpp
 * @brief DirectXCommon の、確かめるための機能（デバッグレイヤーの知らせの受け口・画面の保存・GPU のメモリの量）を実装するファイル
 * @note 自動テストが不具合を見つけた時に使う。ふだんの描画では何もしない
 */
#include "DirectXCommon.h"
#include "DirectXTex.h"
#include "Logger.h"
#include "StringUtility.h"
#include <filesystem>

namespace engine {

/** @brief デバッグレイヤーの警告とエラーを受け取る関数を登録する */
void DirectXCommon::SetDebugMessageHandler(DebugMessageHandler handler) {
    debugMessageHandler_ = std::move(handler);
}

/** @brief デバッグレイヤーのエラーで止めるかを決める（Debug ビルドの時だけ効く） */
void DirectXCommon::SetBreakOnDebugErrors(bool enabled) {
#ifdef _DEBUG
    if (infoQueue_) {
        infoQueue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, enabled ? TRUE : FALSE);
        infoQueue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, enabled ? TRUE : FALSE);
    }
#else
    (void)enabled;
#endif
}

/** @brief このアプリが今使っている GPU のメモリの量 */
uint64_t DirectXCommon::GetGpuMemoryUsage() const {
    if (!adapter_) {
        return 0;
    }
    // GPU に載っている分（ローカル）だけを数える
    DXGI_QUERY_VIDEO_MEMORY_INFO info {};
    if (FAILED(adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
        return 0;
    }
    return info.CurrentUsage;
}

/** @brief 頼まれていれば、今のバックバッファを PNG で保存する */
void DirectXCommon::CaptureRequestedScreenshot(UINT backBufferIndex) {
    if (screenshotPath_.empty()) {
        return;
    }
    const std::wstring path = std::move(screenshotPath_);
    screenshotPath_.clear();
    // 描き終えたバックバッファは画面へ出す状態になっているので、その状態のまま写し取って戻す
    DirectX::ScratchImage image;
    HRESULT hr = DirectX::CaptureTexture(commandQueue_.Get(), swapChainResources_[backBufferIndex].Get(), false, image,
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT);
    if (FAILED(hr) || image.GetImageCount() == 0) {
        Logger::LogWarning("画面を写し取れませんでした: " + StringUtility::ConvertString(path));
        return;
    }
    // 保存先のフォルダが無ければ作ってから、PNG で書き出す
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    hr = DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_FORCE_SRGB,
        DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path.c_str());
    if (FAILED(hr)) {
        Logger::LogWarning("画面を保存できませんでした: " + StringUtility::ConvertString(path));
    }
}

} // namespace engine
