/**
 * @file PipelineCache.cpp
 * @brief PipelineCache のパイプラインの保存と再利用（ID3D12PipelineLibrary）、ホットリロード用の記録を実装するファイル
 * @note ファイルの書き換えを調べて作り直す処理は PipelineCacheHotReload.cpp に分けている（クラスは同じ）
 */
#include "PipelineCache.h"
#include "DirectXCommon.h"
#include "EngineAssert.h"
#include "Logger.h"
#include <cstring>
#include <format>
#include <fstream>
#include <initializer_list>
#include <system_error>

namespace engine::graphics {
namespace {
/** @brief 保存するファイルの場所（実行する場所からの相対） */
constexpr const char* kLibraryFilePath = "cache/pipeline_cache.bin";
/** @brief これより大きいファイルは読まずに空から作り直す（古い設定の物がたまり続けないようにする） */
constexpr std::uintmax_t kMaxLibraryFileBytes = 64ull * 1024ull * 1024ull;
/** @brief ハッシュ（FNV-1a 64bit）の初期値と掛ける数 */
constexpr uint64_t kHashOffset = 14695981039346656037ull;
constexpr uint64_t kHashPrime = 1099511628211ull;
/** @brief ルートシグネチャに付ける目印の GUID */
constexpr GUID kRootSignatureHashGuid = { 0x7c4b2e19, 0x5a3d, 0x4f61, { 0x9e, 0x08, 0x2b, 0x6d, 0x41, 0xc7, 0x93, 0x5a } };

/** @brief バイト列をハッシュへ混ぜる */
uint64_t HashBytes(uint64_t hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kHashPrime;
    }
    return hash;
}

/** @brief シェーダーのバイトコードの大きさと中身をハッシュへ混ぜる */
uint64_t HashBytecode(uint64_t hash, const D3D12_SHADER_BYTECODE& bytecode) {
    hash = HashBytes(hash, &bytecode.BytecodeLength, sizeof(bytecode.BytecodeLength));
    return bytecode.pShaderBytecode != nullptr ? HashBytes(hash, bytecode.pShaderBytecode, bytecode.BytecodeLength) : hash;
}

/** @brief ルートシグネチャに付けた目印を読む（付いていなければ 0） */
uint64_t RootSignatureHash(ID3D12RootSignature* rootSignature) {
    uint64_t hash = 0;
    UINT size = sizeof(hash);
    if (rootSignature == nullptr || FAILED(rootSignature->GetPrivateData(kRootSignatureHashGuid, &size, &hash))) {
        return 0;
    }
    return hash;
}

/** @brief ハッシュから入れ物の中の名前を作る */
std::wstring MakePipelineName(uint64_t hash) {
    return std::format(L"pso_{:016x}", hash);
}

/** @brief グラフィックス用の設定を、ポインタの先の中身まで含めてハッシュにする */
uint64_t HashGraphicsDesc(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
    // ポインタは毎回違うので消し、指している中身を別に混ぜる
    D3D12_GRAPHICS_PIPELINE_STATE_DESC flat = desc;
    flat.pRootSignature = nullptr;
    flat.VS = {};
    flat.PS = {};
    flat.DS = {};
    flat.HS = {};
    flat.GS = {};
    flat.StreamOutput = {};
    flat.InputLayout = {};
    flat.CachedPSO = {};
    uint64_t hash = HashBytes(kHashOffset, &flat, sizeof(flat));
    for (const D3D12_SHADER_BYTECODE* stage : { &desc.VS, &desc.PS, &desc.DS, &desc.HS, &desc.GS }) {
        hash = HashBytecode(hash, *stage);
    }
    // 頂点の並びは名前の文字と、名前以外の項目を混ぜる
    for (UINT i = 0; i < desc.InputLayout.NumElements; ++i) {
        D3D12_INPUT_ELEMENT_DESC element = desc.InputLayout.pInputElementDescs[i];
        if (element.SemanticName != nullptr) {
            hash = HashBytes(hash, element.SemanticName, std::strlen(element.SemanticName));
        }
        element.SemanticName = nullptr;
        hash = HashBytes(hash, &element, sizeof(element));
    }
    const uint64_t rootHash = RootSignatureHash(desc.pRootSignature);
    return HashBytes(hash, &rootHash, sizeof(rootHash));
}

/** @brief コンピュート用の設定を、シェーダーとルートシグネチャの中身まで含めてハッシュにする */
uint64_t HashComputeDesc(const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc) {
    uint64_t hash = HashBytes(kHashOffset, &desc.NodeMask, sizeof(desc.NodeMask));
    hash = HashBytes(hash, &desc.Flags, sizeof(desc.Flags));
    hash = HashBytecode(hash, desc.CS);
    const uint64_t rootHash = RootSignatureHash(desc.pRootSignature);
    return HashBytes(hash, &rootHash, sizeof(rootHash));
}

/** @brief ファイルを丸ごと読む（無い・大きすぎる時は空） */
std::vector<char> ReadLibraryFile() {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(kLibraryFilePath, error);
    if (error || size == 0 || size > kMaxLibraryFileBytes) {
        return {};
    }
    std::ifstream file(kLibraryFilePath, std::ios::binary);
    std::vector<char> data(static_cast<size_t>(size));
    if (!file.read(data.data(), static_cast<std::streamsize>(data.size()))) {
        return {};
    }
    return data;
}
} // namespace

/** @brief 保存したファイルを読み、パイプラインの入れ物を作る */
void PipelineCache::Initialize(engine::DirectXCommon* dxCommon) {
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;
    CreateLibrary(dxCommon->GetDevice());
}

/** @brief 保存したファイルの中身から入れ物を作る（だめなら空の入れ物を作る） */
void PipelineCache::CreateLibrary(ID3D12Device* device) {
    // 入れ物は ID3D12Device1 から作る（使えない環境では保存せずに毎回作る）
    Microsoft::WRL::ComPtr<ID3D12Device1> device1;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1)))) {
        Logger::LogInfo("PipelineCache: この環境ではパイプラインを保存できないので、毎回作る");
        return;
    }
    // ドライバや GPU が変わっていると読めないので、その時は空から作る
    libraryBlob_ = ReadLibraryFile();
    if (!libraryBlob_.empty()
        && SUCCEEDED(device1->CreatePipelineLibrary(libraryBlob_.data(), libraryBlob_.size(), IID_PPV_ARGS(&library_)))) {
        Logger::LogInfo(std::format("PipelineCache: 保存したパイプラインを読んだ ({} バイト)", libraryBlob_.size()));
        return;
    }
    libraryBlob_.clear();
    if (FAILED(device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&library_)))) {
        library_.Reset();
        Logger::LogInfo("PipelineCache: パイプラインの入れ物を作れなかったので、毎回作る");
    }
}

/** @brief 新しく入れた物があればファイルへ書き、入れ物を手放す */
void PipelineCache::Finalize() {
    std::scoped_lock lock(libraryMutex_);
    if (library_ && libraryDirty_) {
        // 入れ物の中身をまとめて書き出す（前のファイルの中身も含まれる）
        const SIZE_T size = library_->GetSerializedSize();
        std::vector<char> data(size);
        if (size > 0 && SUCCEEDED(library_->Serialize(data.data(), size))) {
            std::error_code error;
            std::filesystem::create_directories(std::filesystem::path(kLibraryFilePath).parent_path(), error);
            std::ofstream file(kLibraryFilePath, std::ios::binary | std::ios::trunc);
            file.write(data.data(), static_cast<std::streamsize>(data.size()));
        }
    }
    library_.Reset();
    libraryBlob_.clear();
    libraryDirty_ = false;
    {
        std::unique_lock trackedLock(trackedMutex_);
        tracked_.clear();
    }
    anyReplaced_ = false;
    dxCommon_ = nullptr;
}

/** @brief 名前で保存した物を探し、無ければ作って保存する */
template <class Desc, class LoadFn, class CreateFn>
Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineCache::LoadOrCreate(const std::wstring& name, const Desc& desc, LoadFn load, CreateFn create) {
    const auto start = std::chrono::steady_clock::now();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    bool hit = false;
    // 入れ物は同時に触らない（シーンの初期化が別のスレッドから来ることがある）
    if (library_) {
        std::scoped_lock lock(libraryMutex_);
        hit = SUCCEEDED(load(name.c_str(), desc, pipeline));
    }
    if (!hit) {
        pipeline = create(desc);
        if (pipeline && library_) {
            std::scoped_lock lock(libraryMutex_);
            // 同じ名前で中身の違う物が先に入っていた時は入らない（その時は保存せずに使う）
            if (SUCCEEDED(library_->StorePipeline(name.c_str(), pipeline.Get()))) {
                libraryDirty_ = true;
            }
        }
    }
    const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::scoped_lock lock(libraryMutex_);
    ++(hit ? stats_.hits : stats_.misses);
    stats_.createMilliseconds += milliseconds;
    return pipeline;
}

/** @brief グラフィックス用のパイプラインを、保存した物があれば取り出し、無ければ作って保存する */
Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineCache::CreateGraphics(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
    const auto load = [this](const wchar_t* name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d, Microsoft::WRL::ComPtr<ID3D12PipelineState>& out) {
        return library_->LoadGraphicsPipeline(name, &d, IID_PPV_ARGS(&out));
    };
    const auto create = [device](const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d) {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> created;
        device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&created));
        return created;
    };
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline = LoadOrCreate(MakePipelineName(HashGraphicsDesc(desc)), desc, load, create);
#ifdef USE_IMGUI
    TrackGraphics(pipeline, desc);
#endif
    return pipeline;
}

/** @brief コンピュート用のパイプラインを、保存した物があれば取り出し、無ければ作って保存する */
Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineCache::CreateCompute(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc) {
    const auto load = [this](const wchar_t* name, const D3D12_COMPUTE_PIPELINE_STATE_DESC& d, Microsoft::WRL::ComPtr<ID3D12PipelineState>& out) {
        return library_->LoadComputePipeline(name, &d, IID_PPV_ARGS(&out));
    };
    const auto create = [device](const D3D12_COMPUTE_PIPELINE_STATE_DESC& d) {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> created;
        device->CreateComputePipelineState(&d, IID_PPV_ARGS(&created));
        return created;
    };
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline = LoadOrCreate(MakePipelineName(HashComputeDesc(desc)), desc, load, create);
#ifdef USE_IMGUI
    TrackCompute(pipeline, desc);
#endif
    return pipeline;
}

/** @brief ルートシグネチャに、シリアライズした中身から作った目印を付ける */
void PipelineCache::TagRootSignature(ID3D12RootSignature* rootSignature, ID3DBlob* serialized) {
    if (rootSignature == nullptr || serialized == nullptr) {
        return;
    }
    const uint64_t hash = HashBytes(kHashOffset, serialized->GetBufferPointer(), serialized->GetBufferSize());
    rootSignature->SetPrivateData(kRootSignatureHashGuid, sizeof(hash), &hash);
}

/** @brief 保存と再利用の数を返す */
PipelineCache::Stats PipelineCache::GetStats() const {
    std::scoped_lock lock(libraryMutex_);
    return stats_;
}

/** @brief 作り直した後のパイプラインを引く */
ID3D12PipelineState* PipelineCache::ResolveReplacement(ID3D12PipelineState* pipeline) const {
    // 1つも作り直していなければ表を引かない（ふだんの描画の重さを増やさない）
    if (pipeline == nullptr || !anyReplaced_.load(std::memory_order_acquire)) {
        return pipeline;
    }
    std::shared_lock lock(trackedMutex_);
    const auto found = tracked_.find(pipeline);
    return (found != tracked_.end() && found->second.current) ? found->second.current.Get() : pipeline;
}

/** @brief 覚えたパイプラインが消えた時に記録を消すよう頼む（頼めない環境なら false） */
bool PipelineCache::WatchDestruction(ID3D12PipelineState* pipeline) {
    Microsoft::WRL::ComPtr<ID3DDestructionNotifier> notifier;
    if (FAILED(pipeline->QueryInterface(IID_PPV_ARGS(&notifier)))) {
        return false;
    }
    UINT callbackId = 0;
    return SUCCEEDED(notifier->RegisterDestructionCallback(&PipelineCache::OnPipelineDestroyed, pipeline, &callbackId));
}

/** @brief パイプラインが消える時に D3D12 から呼ばれ、そのパイプラインの記録を消す */
void WINAPI PipelineCache::OnPipelineDestroyed(void* pipeline) {
    PipelineCache* cache = GetInstance();
    std::unique_lock lock(cache->trackedMutex_);
    cache->tracked_.erase(static_cast<ID3D12PipelineState*>(pipeline));
}

/** @brief バイトコードから元のファイルを引いて、シェーダー1段ぶんの元を作る（引けなければ空） */
PipelineCache::ShaderStageSource PipelineCache::FindStageSource(const D3D12_SHADER_BYTECODE& bytecode) const {
    ShaderStageSource source;
    if (dxCommon_ != nullptr && bytecode.pShaderBytecode != nullptr) {
        dxCommon_->FindShaderSource(bytecode.pShaderBytecode, source.path, source.profile);
    }
    return source;
}

/** @brief グラフィックス用のパイプラインの設定と元のシェーダーを覚える（ホットリロード用） */
void PipelineCache::TrackGraphics(const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
    if (!pipeline) {
        return;
    }
    TrackedPipeline tracked;
    // 使っている段は全部、元のファイルが分かる時だけ覚える（分からない段があると作り直せない）
    for (const D3D12_SHADER_BYTECODE* stage : { &desc.VS, &desc.PS, &desc.DS, &desc.HS, &desc.GS }) {
        ShaderStageSource source = FindStageSource(*stage);
        if (stage->pShaderBytecode != nullptr && source.path.empty()) {
            return;
        }
        tracked.stages.push_back(std::move(source));
    }
    // 作った側がパイプラインを手放したら記録も消す（頼めない環境では、アドレスが使い回されないよう持ち続ける）
    if (!WatchDestruction(pipeline.Get())) {
        tracked.original = pipeline;
    }
    tracked.rootSignature = desc.pRootSignature;
    tracked.graphicsDesc = desc;
    // 頂点の並びは名前の文字ごと写しておく（元の配列は作った側の物なので、作り直す時には残っていないかもしれない）
    // 名前を指すポインタは、写した文字列が動くと変わるので覚えず、作り直す時に付け直す
    for (UINT i = 0; i < desc.InputLayout.NumElements; ++i) {
        D3D12_INPUT_ELEMENT_DESC element = desc.InputLayout.pInputElementDescs[i];
        tracked.semanticNames.emplace_back(element.SemanticName != nullptr ? element.SemanticName : "");
        element.SemanticName = nullptr;
        tracked.inputElements.push_back(element);
    }
    std::unique_lock lock(trackedMutex_);
    tracked_[pipeline.Get()] = std::move(tracked);
}

/** @brief コンピュート用のパイプラインの設定と元のシェーダーを覚える（ホットリロード用） */
void PipelineCache::TrackCompute(const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc) {
    if (!pipeline) {
        return;
    }
    ShaderStageSource source = FindStageSource(desc.CS);
    if (source.path.empty()) {
        return;
    }
    TrackedPipeline tracked;
    tracked.isCompute = true;
    if (!WatchDestruction(pipeline.Get())) {
        tracked.original = pipeline;
    }
    tracked.rootSignature = desc.pRootSignature;
    tracked.computeDesc = desc;
    tracked.stages.push_back(std::move(source));
    std::unique_lock lock(trackedMutex_);
    tracked_[pipeline.Get()] = std::move(tracked);
}

} // namespace engine::graphics
