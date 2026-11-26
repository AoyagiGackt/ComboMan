/**
 * @file PipelineCacheHotReload.cpp
 * @brief PipelineCache のシェーダーのホットリロード（ファイルの書き換えを調べ、コンパイルし直してパイプラインを作り直す）を実装するファイル
 * @note クラスは PipelineCache のまま、ホットリロードの処理だけをこのファイルに分けている
 */
#include "PipelineCache.h"
#include "DirectXCommon.h"
#include "Logger.h"
#include "StringUtility.h"
#include <algorithm>
#include <format>
#include <system_error>

namespace engine::graphics {
namespace {
/** @brief ファイルの書き換えを調べる間隔（毎フレーム調べるとファイルの問い合わせが重い） */
constexpr std::chrono::milliseconds kCheckInterval(500);
/** @brief 共通部分のファイルを探すシェーダーのフォルダ */
constexpr const wchar_t* kShaderFolder = L"Resources/shaders";
/** @brief 共通部分のファイルの拡張子（書き換わったら全部のシェーダーを読み直す） */
constexpr const wchar_t* kIncludeExtension = L".hlsli";

/** @brief キャッシュと同じ、ファイルとプロファイルの組み合わせのキー */
std::wstring MakeStageKey(const std::wstring& path, const std::wstring& profile) {
    return path + L"|" + profile;
}

/** @brief ファイルの書き換えの時刻を読む（読めなければ false） */
bool ReadWriteTime(const std::filesystem::path& path, std::filesystem::file_time_type& outTime) {
    std::error_code error;
    outTime = std::filesystem::last_write_time(path, error);
    return !error;
}
} // namespace

/** @brief シェーダーのファイルが書き換わったかを調べ、書き換わっていればパイプラインを作り直す */
void PipelineCache::CheckHotReload() {
#ifdef USE_IMGUI
    if (!hotReloadEnabled_ || dxCommon_ == nullptr) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - lastCheck_ < kCheckInterval) {
        return;
    }
    lastCheck_ = now;

    // 今の時刻を集め、前に見た時刻と比べる（初めての時は覚えるだけ）
    FileTimes current = CollectFileTimes();
    const bool firstCheck = fileTimes_.empty();
    std::vector<std::wstring> changed;
    bool includeChanged = false;
    for (const auto& [path, time] : current) {
        const auto previous = fileTimes_.find(path);
        if (previous == fileTimes_.end() || previous->second == time) {
            continue;
        }
        changed.push_back(path);
        includeChanged |= std::filesystem::path(path).extension() == kIncludeExtension;
    }
    fileTimes_ = std::move(current);
    if (firstCheck || changed.empty()) {
        return;
    }
    RebuildChanged(changed, includeChanged);
#endif
}

/** @brief 調べる対象のファイル（覚えたシェーダーと、シェーダーのフォルダの共通ファイル）の今の時刻を集める */
PipelineCache::FileTimes PipelineCache::CollectFileTimes() const {
    FileTimes times;
    std::filesystem::file_time_type time;
    {
        std::shared_lock lock(trackedMutex_);
        for (const auto& [pipeline, tracked] : tracked_) {
            for (const ShaderStageSource& stage : tracked.stages) {
                if (!stage.path.empty() && !times.contains(stage.path) && ReadWriteTime(stage.path, time)) {
                    times.emplace(stage.path, time);
                }
            }
        }
    }
    // 共通部分のファイルはどのシェーダーが読んでいるか分からないので、フォルダの中を全部見る
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(kShaderFolder, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && it->path().extension() == kIncludeExtension && ReadWriteTime(it->path(), time)) {
            times.emplace(it->path().generic_wstring(), time);
        }
    }
    return times;
}

/** @brief 覚えたパイプラインのうち、書き換わったファイルを使う物を作り直す */
void PipelineCache::RebuildChanged(const std::vector<std::wstring>& changedFiles, bool rebuildAll) {
    // 作り直す物を選び、使う段のシェーダーを1回ずつコンパイルし直す（失敗した段は空にして、それを使う物は作り直さない）
    std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<IDxcBlob>> compiled;
    std::vector<ID3D12PipelineState*> targets;
    {
        std::shared_lock lock(trackedMutex_);
        for (const auto& [pipeline, tracked] : tracked_) {
            bool uses = rebuildAll;
            for (const ShaderStageSource& stage : tracked.stages) {
                if (stage.path.empty()) {
                    continue;
                }
                const bool stageChanged = rebuildAll
                    || std::find(changedFiles.begin(), changedFiles.end(), stage.path) != changedFiles.end();
                if (!stageChanged) {
                    continue;
                }
                uses = true;
                const std::wstring key = MakeStageKey(stage.path, stage.profile);
                if (!compiled.contains(key)) {
                    compiled.emplace(key, dxCommon_->CompileShaderFromSource(stage.path, stage.profile.c_str()));
                }
            }
            if (uses) {
                targets.push_back(pipeline);
            }
        }
    }

    // 新しいパイプラインを作る（失敗した物は今のまま使い続ける）
    std::vector<std::pair<ID3D12PipelineState*, Microsoft::WRL::ComPtr<ID3D12PipelineState>>> rebuilt;
    {
        std::shared_lock lock(trackedMutex_);
        for (ID3D12PipelineState* key : targets) {
            Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline = Rebuild(tracked_.at(key), compiled);
            if (pipeline) {
                rebuilt.emplace_back(key, pipeline);
            }
        }
    }
    if (rebuilt.empty()) {
        Logger::LogWarning("PipelineCache: シェーダーが書き換わったが、作り直せたパイプラインは無い（コンパイルのエラーはログを見る）");
        return;
    }

    // 前に作り直した物を GPU が使い終えてから入れ替える
    dxCommon_->WaitForGpu();
    {
        std::unique_lock lock(trackedMutex_);
        for (auto& [key, pipeline] : rebuilt) {
            tracked_.at(key).current = pipeline;
        }
    }
    anyReplaced_.store(true, std::memory_order_release);
    {
        std::scoped_lock lock(libraryMutex_);
        stats_.reloaded += static_cast<uint32_t>(rebuilt.size());
    }
    Logger::LogInfo(std::format("PipelineCache: シェーダーを読み直し、パイプラインを {} 個作り直した", rebuilt.size()));
}

/** @brief 覚えたパイプライン1つを、コンパイルし直したシェーダーで作り直す（だめなら空） */
Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineCache::Rebuild(const TrackedPipeline& tracked,
    const std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<IDxcBlob>>& compiled) const {
    // 段ごとに、コンパイルし直した物があればそれを、無ければ今キャッシュにある物を使う
    std::vector<Microsoft::WRL::ComPtr<IDxcBlob>> blobs;
    for (const ShaderStageSource& stage : tracked.stages) {
        if (stage.path.empty()) {
            blobs.emplace_back();
            continue;
        }
        const auto found = compiled.find(MakeStageKey(stage.path, stage.profile));
        Microsoft::WRL::ComPtr<IDxcBlob> blob = found != compiled.end()
            ? found->second
            : dxCommon_->CompileShader(stage.path, stage.profile.c_str());
        if (!blob) {
            return nullptr;
        }
        blobs.push_back(blob);
    }
    const auto toBytecode = [](const Microsoft::WRL::ComPtr<IDxcBlob>& blob) {
        return blob ? D3D12_SHADER_BYTECODE { blob->GetBufferPointer(), blob->GetBufferSize() } : D3D12_SHADER_BYTECODE {};
    };

    ID3D12Device* device = dxCommon_->GetDevice();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    if (tracked.isCompute) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = tracked.computeDesc;
        desc.pRootSignature = tracked.rootSignature.Get();
        desc.CS = toBytecode(blobs[0]);
        desc.CachedPSO = {};
        device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline));
        return pipeline;
    }
    // 頂点の並びの名前は、覚えておいた文字列を指し直す
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements = tracked.inputElements;
    for (size_t i = 0; i < elements.size(); ++i) {
        elements[i].SemanticName = tracked.semanticNames[i].c_str();
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = tracked.graphicsDesc;
    desc.pRootSignature = tracked.rootSignature.Get();
    desc.InputLayout = { elements.empty() ? nullptr : elements.data(), static_cast<UINT>(elements.size()) };
    desc.VS = toBytecode(blobs[0]);
    desc.PS = toBytecode(blobs[1]);
    desc.DS = toBytecode(blobs[2]);
    desc.HS = toBytecode(blobs[3]);
    desc.GS = toBytecode(blobs[4]);
    desc.CachedPSO = {};
    device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline));
    return pipeline;
}

} // namespace engine::graphics
