/**
 * @file ParticleManager.cpp
 * @brief GPUで更新するパーティクル（ParticleManager）の資源と更新・描画の実装
 * @note 1フレームの流れ（グループごと）:
 *       空にする（頼まれた時だけ）→ 頼まれた粒を出す → エミッター → 動かして寿命を判定し、尽きた枠を空き一覧へ返す → 描く
 */
#include "ParticleManager.h"
#include "EngineAssert.h"
#include "EngineConstants.h"
#include "GraphicsTuning.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>
#include <d3dx12.h>

namespace engine::graphics {
namespace {

// GPUパーティクルの処理単位と四角形インデックス
constexpr unsigned int kComputeThreadsPerGroup = 64;
constexpr unsigned int kQuadIndexCount = 6;
// 空き一覧の数え上げのバッファの要素数（0 番が空きの数。残りは予備）
constexpr uint32_t kCounterElementCount = 4;
// 寿命の見込みに足す余裕（GPU が寿命を判定して枠を返し終えるまで更新を続ける）
constexpr float kAliveMargin = 0.1f;
// ソフトパーティクルで、後ろの物にこの奥行きまで近づくと薄くなり始める（graphics_tuning.json の particle.softRange）
constexpr float kDefaultSoftRange = 0.6f;

/** @brief 計算用シェーダーのスレッドグループ数（1スレッドで1件） */
UINT ThreadGroupsFor(uint32_t count) {
    return (count + kComputeThreadsPerGroup - 1) / kComputeThreadsPerGroup;
}

/** @brief ParticleInit.CS と ParticleSpawn.CS に渡す件数（定数バッファは 16 バイト単位） */
struct CountConstants {
    uint32_t count = 0;
    uint32_t pad[3] = {};
};

} // namespace

using namespace Microsoft::WRL;

// ══════════════════════════════════════════════════════
// 初期化とグループ管理
// ══════════════════════════════════════════════════════

/** @brief ParticleManager の唯一のインスタンスを返す */
ParticleManager* ParticleManager::GetInstance() {
    static ParticleManager sInstance;
    return &sInstance;
}

/** @brief 描画用とコンピュート用のルートシグネチャ・PSO・板ポリゴンを作成して初期化する */
void ParticleManager::Initialize(DirectXCommon* dxCommon) {
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;

    CreateRootSignature();
    CreatePipelineState();
    CreateComputePipelines();
    CreateQuadGeometry();
}

/** @brief 全グループを破棄し GPU リソースを解放する */
void ParticleManager::Finalize() {
    particleGroups_.clear();
    sceneDepthCopies_ = {};

    quadIndexBuffer_.Reset();
    quadVertexBuffer_.Reset();
    updatePass_ = {};
    emitPass_ = {};
    spawnPass_ = {};
    initPass_ = {};
    graphicsPipelineStateAlpha_.Reset();
    graphicsPipelineState_.Reset();
    rootSignature_.Reset();
    dxCommon_ = nullptr;
}

/** @brief 名前とテクスチャを指定してパーティクルグループを作る（同じ名前は作れない） */
void ParticleManager::CreateParticleGroup(const std::string& name,
    const std::string& textureFilePath) {
    ENGINE_ASSERT(!particleGroups_.contains(name));

    ParticleGroup& group = particleGroups_[name];
    group.textureFilePath = textureFilePath;

    TextureManager::GetInstance()->LoadTexture(textureFilePath);

    CreateParticleBuffers(group);
    CreateParticleInstancingResource(group);
    // 中身は最初の更新で GPU が空にする（CPU から初期値を送らない）
    group.needsReset = true;
    group.emitter.lifeTime = 1.0f; // デフォルト 1 秒
}

/** @brief 粒の状態・空き一覧・空きの数のバッファを GPU 上に作る */
void ParticleManager::CreateParticleBuffers(ParticleGroup& group) {
    ID3D12Device* device = dxCommon_->GetDevice();
    group.particleStateBuffer = pipelineHelper::CreateUnorderedAccessBuffer(device,
        sizeof(GPUParticleState) * ParticleGroup::kNumMaxInstance);
    group.freeListBuffer = pipelineHelper::CreateUnorderedAccessBuffer(device,
        sizeof(uint32_t) * ParticleGroup::kNumMaxInstance);
    group.counterBuffer = pipelineHelper::CreateUnorderedAccessBuffer(device,
        sizeof(int32_t) * kCounterElementCount);
}

/** @brief パーティクルのインスタンス描画用のリソースを生成する */
void ParticleManager::CreateParticleInstancingResource(ParticleGroup& group) {
    ID3D12Device* device = dxCommon_->GetDevice();
    const UINT64 instanceSize = sizeof(ParticleForGPU) * ParticleGroup::kNumMaxInstance;

    // instancingResource: DEFAULT heap, UAV (CS が書く)
    group.instancingResource = pipelineHelper::CreateUnorderedAccessBuffer(device, instanceSize);

    // SRV for instancingResource (VS が t0 として読む)
    group.srvIndex = SrvManager::GetInstance()->Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE cpuH = SrvManager::GetInstance()->GetCPUDescriptorHandle(group.srvIndex);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc { };
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = ParticleGroup::kNumMaxInstance;
    srvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
    device->CreateShaderResourceView(group.instancingResource.Get(), &srvDesc, cpuH);
}

// ══════════════════════════════════════════════════════
// GPU更新
// ══════════════════════════════════════════════════════

/** @brief 今のフレームの領域へ書いてアドレスを返す */
D3D12_GPU_VIRTUAL_ADDRESS ParticleManager::UploadFrameData(const void* data, size_t size) {
    return dxCommon_->GetFrameUploadAllocator().Allocate(data, size);
}

/** @brief カメラからビルボード行列を作り、更新の CS の定数を今のフレームの領域へ書く */
D3D12_GPU_VIRTUAL_ADDRESS ParticleManager::UploadCSConstants(Camera* camera, float dt) {
    Matrix4x4 billboard = MakeIdentity4x4();
    const Matrix4x4 cameraView = camera->GetViewMatrix();
    billboard.m[0][0] = cameraView.m[0][0];
    billboard.m[0][1] = cameraView.m[1][0];
    billboard.m[0][2] = cameraView.m[2][0];
    billboard.m[1][0] = cameraView.m[0][1];
    billboard.m[1][1] = cameraView.m[1][1];
    billboard.m[1][2] = cameraView.m[2][1];
    billboard.m[2][0] = cameraView.m[0][2];
    billboard.m[2][1] = cameraView.m[1][2];
    billboard.m[2][2] = cameraView.m[2][2];

    CSConstants constants {};
    constants.billboard = billboard;
    constants.viewProj = Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    constants.deltaTime = dt;
    constants.maxParticles = ParticleGroup::kNumMaxInstance;
    constants.frame = frameCounter_;
    return UploadFrameData(&constants, sizeof(constants));
}

/** @brief インスタンス用のリソースを UAV の状態へ遷移させる */
void ParticleManager::TransitionInstancingToUAV(ParticleGroup& group, ID3D12GraphicsCommandList* cmd) {
    if (!group.instancingInSRV) {
        return;
    }
    DirectXCommon::TransitionBarrier(cmd, group.instancingResource.Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    group.instancingInSRV = false;
}

/** @brief 作成直後のバッファを UAV の状態へ移す */
void ParticleManager::TransitionBuffersToUAV(ParticleGroup& group, ID3D12GraphicsCommandList* cmd) {
    if (!group.buffersFresh) {
        return;
    }
    constexpr size_t kBufferCount = 3; // 粒・空き一覧・空きの数
    const std::array<ID3D12Resource*, kBufferCount> buffers = { group.particleStateBuffer.Get(), group.freeListBuffer.Get(), group.counterBuffer.Get() };
    std::array<D3D12_RESOURCE_BARRIER, kBufferCount> barriers {};
    for (size_t i = 0; i < buffers.size(); ++i) {
        barriers[i] = DirectXCommon::MakeTransitionBarrier(buffers[i], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    cmd->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    group.buffersFresh = false;
}

/** @brief 粒の状態・空き一覧・空きの数の書き込みが終わるのを待つ */
void ParticleManager::UavBarrier(ID3D12GraphicsCommandList* cmd) {
    // 対象を決めない UAV バリアは、すべての UAV の書き込みの完了を待つ
    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = nullptr;
    cmd->ResourceBarrier(1, &barrier);
}

/** @brief 全部の粒を消し、空き一覧に全部の枠を積む */
void ParticleManager::DispatchInitCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd) {
    namespace slot = pipelineHelper::particleComputeSlot::init;
    CountConstants constants {};
    constants.count = ParticleGroup::kNumMaxInstance;
    cmd->SetComputeRootSignature(initPass_.rootSignature.Get());
    cmd->SetPipelineState(initPass_.pipeline.Get());
    cmd->SetComputeRootConstantBufferView(slot::kConstants, UploadFrameData(&constants, sizeof(constants)));
    cmd->SetComputeRootUnorderedAccessView(slot::kParticles, group.particleStateBuffer->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(slot::kInstances, group.instancingResource->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(slot::kFreeList, group.freeListBuffer->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(slot::kCounter, group.counterBuffer->GetGPUVirtualAddress());
    cmd->Dispatch(ThreadGroupsFor(ParticleGroup::kNumMaxInstance), 1, 1);
    UavBarrier(cmd);
    group.needsReset = false;
}

/** @brief 出してほしい粒の一覧を送り、空き一覧から枠を取って書き込む */
void ParticleManager::DispatchSpawnCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd) {
    if (group.pendingSpawns.empty()) {
        return;
    }
    namespace slot = pipelineHelper::particleComputeSlot::spawn;
    // 送るのは頼まれた粒の分だけ（粒全体の状態は GPU にあるので送らない）
    const uint32_t count = static_cast<uint32_t>(group.pendingSpawns.size());
    CountConstants constants {};
    constants.count = count;
    const D3D12_GPU_VIRTUAL_ADDRESS requests = UploadFrameData(group.pendingSpawns.data(), sizeof(GPUParticleState) * count);
    cmd->SetComputeRootSignature(spawnPass_.rootSignature.Get());
    cmd->SetPipelineState(spawnPass_.pipeline.Get());
    cmd->SetComputeRootConstantBufferView(slot::kConstants, UploadFrameData(&constants, sizeof(constants)));
    cmd->SetComputeRootShaderResourceView(slot::kRequests, requests);
    cmd->SetComputeRootUnorderedAccessView(slot::kParticles, group.particleStateBuffer->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(slot::kFreeList, group.freeListBuffer->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(slot::kCounter, group.counterBuffer->GetGPUVirtualAddress());
    cmd->Dispatch(ThreadGroupsFor(count), 1, 1);
    UavBarrier(cmd);
    group.pendingSpawns.clear();
}

/** @brief 発生頻度に応じて発射タイミングを判定し、来ていればエミッターの CS で粒を出す */
void ParticleManager::DispatchEmitCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd, float dt) {
    // 発生間隔ごとに発射の合図を立てる（間隔0なら一度きりの発射）
    Emitter& e = group.emitter;
    e.time = *reinterpret_cast<uint32_t*>(&group.groupTime); // float ビット列を uint でそのまま渡す
    e.seed++;
    if (e.frequency > 0.0f) {
        e.frequencyTime += dt;
        if (e.frequency <= e.frequencyTime) {
            e.frequencyTime -= e.frequency;
            e.emit = 1;
        } else {
            e.emit = 0;
        }
    }
    if (e.emit == 0 || e.count == 0) {
        return;
    }
    namespace slot = pipelineHelper::particleComputeSlot::emit;
    const uint32_t count = (std::min)(e.count, ParticleGroup::kNumMaxInstance);
    group.aliveUntil = (std::max)(group.aliveUntil, group.groupTime + e.lifeTime + kAliveMargin);

    // エミッターの CS で、空き一覧から枠を取って新しい粒を書き込む（1スレッドで1粒）
    cmd->SetComputeRootSignature(emitPass_.rootSignature.Get());
    cmd->SetPipelineState(emitPass_.pipeline.Get());
    cmd->SetComputeRootConstantBufferView(slot::kEmitter, UploadFrameData(&e, sizeof(e)));
    cmd->SetComputeRootUnorderedAccessView(slot::kParticles, group.particleStateBuffer->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(slot::kFreeList, group.freeListBuffer->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(slot::kCounter, group.counterBuffer->GetGPUVirtualAddress());
    cmd->Dispatch(ThreadGroupsFor(count), 1, 1);
    UavBarrier(cmd);

    // one-shot (frequency == 0) はディスパッチ後にリセット
    if (e.frequency == 0.0f) {
        e.emit = 0;
    }
}

/** @brief パーティクルの更新用のコンピュートシェーダーを実行する */
void ParticleManager::DispatchUpdateCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd,
    D3D12_GPU_VIRTUAL_ADDRESS constants, D3D12_GPU_VIRTUAL_ADDRESS fields) {
    namespace slot = pipelineHelper::particleComputeSlot::update;
    cmd->SetComputeRootSignature(updatePass_.rootSignature.Get());
    cmd->SetPipelineState(updatePass_.pipeline.Get());
    cmd->SetComputeRootConstantBufferView(slot::kConstants, constants);
    cmd->SetComputeRootConstantBufferView(slot::kLoop, UploadFrameData(&group.loop, sizeof(group.loop)));
    cmd->SetComputeRootConstantBufferView(slot::kFields, fields);
    cmd->SetComputeRootUnorderedAccessView(slot::kParticles, group.particleStateBuffer->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(slot::kInstances, group.instancingResource->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(slot::kFreeList, group.freeListBuffer->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(slot::kCounter, group.counterBuffer->GetGPUVirtualAddress());
    cmd->Dispatch(ThreadGroupsFor(ParticleGroup::kNumMaxInstance), 1, 1);
    UavBarrier(cmd);

    // instancingResource: UAV → NON_PIXEL_SHADER_RESOURCE（VS が SRV として読む）
    DirectXCommon::TransitionBarrier(cmd, group.instancingResource.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    group.instancingInSRV = true;
}

/** @brief グループの粒を描く必要があるか */
bool ParticleManager::NeedsDraw(const ParticleGroup& group) {
    return group.loop.enabled != 0 || group.groupTime < group.aliveUntil;
}

/** @brief グループを更新する必要があるか */
bool ParticleManager::NeedsUpdate(const ParticleGroup& group) {
    return NeedsDraw(group) || group.needsReset || !group.pendingSpawns.empty()
        || group.emitter.frequency > 0.0f || group.emitter.emit != 0;
}

// ══════════════════════════════════════════════════════
// フレーム更新と描画
// ══════════════════════════════════════════════════════

/** @brief 全グループで、空にする・頼まれた粒を出す・エミッター・動かすの順に計算用シェーダーを回す */
void ParticleManager::Update(Camera* camera) {
    const float deltaSeconds = engineConstants::kFrameDeltaTime;
    ++frameCounter_;

    auto* cmd = dxCommon_->GetCommandList();
    // 場はこのフレームに効かせる分をまとめて1回だけ送り、全グループで同じものを使う
    const D3D12_GPU_VIRTUAL_ADDRESS fields = UploadFields(deltaSeconds);
    D3D12_GPU_VIRTUAL_ADDRESS constants = 0;
    for (auto& [name, group] : particleGroups_) {
        group.groupTime += deltaSeconds;
        // 粒も頼みも無いグループは GPU の仕事を積まない
        if (!NeedsUpdate(group)) {
            continue;
        }
        if (constants == 0) {
            constants = UploadCSConstants(camera, deltaSeconds);
        }
        TransitionInstancingToUAV(group, cmd);
        TransitionBuffersToUAV(group, cmd);
        if (group.needsReset) {
            DispatchInitCS(group, cmd);
        }
        DispatchSpawnCS(group, cmd);
        DispatchEmitCS(group, cmd, deltaSeconds);
        DispatchUpdateCS(group, cmd, constants, fields);
    }
}

// ══════════════════════════════════════════════════════
// 場（粒に掛ける力）
// ══════════════════════════════════════════════════════

/** @brief 粒に力を掛ける場を置く */
void ParticleManager::AddField(const ParticleField& field, float seconds) {
    if (seconds > 0.0f) {
        timedFields_.push_back({ field, seconds, seconds });
        return;
    }
    frameFields_.push_back(field);
}

/** @brief グループの粒への場の効き方の倍率を決める */
void ParticleManager::SetFieldScale(const std::string& name, float scale) {
    const auto it = particleGroups_.find(name);
    if (it != particleGroups_.end()) {
        it->second.loop.fieldScale = (std::max)(scale, 0.0f);
    }
}

/** @brief このフレームに効かせる場を集めて今のフレームの領域へ書き、秒数を決めた場の時間を進める */
D3D12_GPU_VIRTUAL_ADDRESS ParticleManager::UploadFields(float dt) {
    FieldConstants constants {};
    const auto push = [&constants](const ParticleField& field) {
        if (constants.count < kMaxFields) {
            constants.fields[constants.count++] = field;
        }
    };
    // このフレームだけの場（追いかける渦など）を先に、秒数を決めた場（衝撃など）を後に入れる
    for (const ParticleField& field : frameFields_) {
        push(field);
    }
    for (const TimedField& timed : timedFields_) {
        // 残りの秒数に合わせて弱める（いきなり消えないように）
        const float fade = timed.total > 0.0f ? timed.remaining / timed.total : 0.0f;
        ParticleField faded = timed.field;
        faded.strength *= fade;
        faded.swirl *= fade;
        push(faded);
    }
    frameFields_.clear();
    for (TimedField& timed : timedFields_) {
        timed.remaining -= dt;
    }
    std::erase_if(timedFields_, [](const TimedField& timed) { return timed.remaining <= 0.0f; });
    return UploadFrameData(&constants, sizeof(constants));
}

/** @brief 全グループの粒子をインスタンシングで描画する */
void ParticleManager::Draw(Camera* camera) {
    const bool anyVisible = std::any_of(particleGroups_.begin(), particleGroups_.end(),
        [](const auto& entry) { return NeedsDraw(entry.second) && entry.second.instancingInSRV; });
    if (!anyVisible) {
        return;
    }
    auto* cmd = dxCommon_->GetCommandList();

    // ソフトパーティクル: 描き終えた物の深度を写し取り、その写しを読む（本物の深度はそのまま深度テストに使う）
    const SceneDepthCopy* depth = (softParticlesEnabled_ && camera != nullptr) ? CopySceneDepth(cmd) : nullptr;
    SoftParticleConstants soft {};
    if (depth != nullptr) {
        static const float kSoftRange = GraphicsTuning::Positive("particle.softRange", kDefaultSoftRange);
        const Matrix4x4& projection = camera->GetProjectionMatrix();
        soft.depthA = projection.m[2][2];
        soft.depthB = projection.m[3][2];
        soft.softRange = kSoftRange;
        soft.enabled = 1;
    }

    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &quadVBV_);
    cmd->IASetIndexBuffer(&quadIBV_);

    SrvManager::GetInstance()->PreDraw();
    cmd->SetGraphicsRootConstantBufferView(pipelineHelper::particleRootSlot::kSoftConstants, UploadFrameData(&soft, sizeof(soft)));

    ID3D12PipelineState* activePSO = nullptr;
    for (auto& [name, group] : particleGroups_) {
        if (!NeedsDraw(group) || !group.instancingInSRV) {
            continue;
        }

        ID3D12PipelineState* pso = group.additiveBlend
            ? graphicsPipelineState_.Get()
            : graphicsPipelineStateAlpha_.Get();
        if (pso != activePSO) {
            cmd->SetPipelineState(pso);
            activePSO = pso;
        }

        cmd->SetGraphicsRootDescriptorTable(
            pipelineHelper::particleRootSlot::kInstances, SrvManager::GetInstance()->GetGPUDescriptorHandle(group.srvIndex));

        const D3D12_GPU_DESCRIPTOR_HANDLE texH = TextureManager::GetInstance()->GetSrvHandleGPU(group.textureFilePath);
        cmd->SetGraphicsRootDescriptorTable(pipelineHelper::particleRootSlot::kTexture, texH);
        // 深度を写し取れなかった時もシェーダーは読まない（enabled=0）が、空けておけないので粒の画像を代わりに置く
        cmd->SetGraphicsRootDescriptorTable(pipelineHelper::particleRootSlot::kSceneDepth,
            depth != nullptr ? SrvManager::GetInstance()->GetGPUDescriptorHandle(depth->srvIndex) : texH);

        // 枠は空き一覧で飛び飛びに使うので全部の枠を描き、死んでいる枠は頂点シェーダーが画面の外へ捨てる
        cmd->DrawIndexedInstanced(kQuadIndexCount, ParticleGroup::kNumMaxInstance, 0, 0, 0);
    }
}

// ══════════════════════════════════════════════════════
// ソフトパーティクルの深度
// ══════════════════════════════════════════════════════

/** @brief 元の深度バッファに合う写し先を用意する（元が替わったか大きさが変わっていたら作り直す） */
ParticleManager::SceneDepthCopy* ParticleManager::PrepareSceneDepthCopy(ID3D12Resource* source) {
    // バックバッファの深度とオフスクリーンの深度で写し先を分ける（毎フレーム行き来しても作り直さない）
    const size_t index = static_cast<size_t>(dxCommon_->GetActiveDepthTarget());
    if (index >= sceneDepthCopies_.size()) {
        return nullptr;
    }
    SceneDepthCopy& copy = sceneDepthCopies_[index];
    const D3D12_RESOURCE_DESC desc = source->GetDesc();
    const bool matches = copy.texture && copy.source == source && copy.width == desc.Width && copy.height == desc.Height;
    if (matches) {
        return &copy;
    }
    // 元の深度は大きさを変える時に GPU を待ってから作り直されるので、写し先もここで作り直してよい
    D3D12_HEAP_PROPERTIES heap {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> texture;
    const HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture));
    if (FAILED(hr)) {
        return nullptr;
    }
    copy.texture = texture;
    copy.source = source;
    copy.width = desc.Width;
    copy.height = desc.Height;
    copy.readable = false;
    if (copy.srvIndex == UINT32_MAX) {
        copy.srvIndex = SrvManager::GetInstance()->Allocate();
    }
    SrvManager::GetInstance()->CreateSrvForDepthTexture(copy.srvIndex, copy.texture.Get());
    return &copy;
}

/** @brief 今描画先にしている深度を写し取り、写しを返す */
const ParticleManager::SceneDepthCopy* ParticleManager::CopySceneDepth(ID3D12GraphicsCommandList* cmd) {
    ID3D12Resource* source = dxCommon_->GetActiveDepthResource();
    if (source == nullptr) {
        return nullptr;
    }
    SceneDepthCopy* copy = PrepareSceneDepthCopy(source);
    if (copy == nullptr) {
        return nullptr;
    }
    // 元は深度を書く状態から写し元へ、写し先は読む状態から写し先へ
    std::array<D3D12_RESOURCE_BARRIER, 2> before {};
    UINT beforeCount = 0;
    before[beforeCount++] = DirectXCommon::MakeTransitionBarrier(source, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (copy->readable) {
        before[beforeCount++] = DirectXCommon::MakeTransitionBarrier(copy->texture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    }
    cmd->ResourceBarrier(beforeCount, before.data());
    cmd->CopyResource(copy->texture.Get(), source);
    // 元は深度を書く状態へ戻し（この後も深度テストに使う）、写し先は読む状態へ
    const std::array<D3D12_RESOURCE_BARRIER, 2> after = {
        DirectXCommon::MakeTransitionBarrier(source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),
        DirectXCommon::MakeTransitionBarrier(copy->texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
    };
    cmd->ResourceBarrier(static_cast<UINT>(after.size()), after.data());
    copy->readable = true;
    return copy;
}

// ══════════════════════════════════════════════════════
// 設定と問い合わせ
// ══════════════════════════════════════════════════════

/** @brief 指定グループのテクスチャを差し替える */
void ParticleManager::SetTexture(const std::string& groupName,
    const std::string& textureFilePath) {
    if (!particleGroups_.contains(groupName)) {
        return;
    }

    ParticleGroup& group = particleGroups_[groupName];
    group.textureFilePath = textureFilePath;
    TextureManager::GetInstance()->LoadTexture(textureFilePath);
}

/** @brief グループの加算合成・アルファ合成を切り替える */
void ParticleManager::SetAdditiveBlend(const std::string& name, bool additive) {
    if (particleGroups_.contains(name)) {
        particleGroups_[name].additiveBlend = additive;
    }
}

/** @brief 指定グループに生存中のパーティクルが残っていそうかを返す */
bool ParticleManager::IsGroupAlive(const std::string& name) const {
    const auto it = particleGroups_.find(name);
    return it != particleGroups_.end() && NeedsDraw(it->second);
}

/** @brief 指定グループのエミッターデータへのポインタを返す */
Emitter* ParticleManager::GetEmitter(const std::string& name) {
    ENGINE_ASSERT(particleGroups_.contains(name));
    return &particleGroups_[name].emitter;
}

} // namespace engine::graphics
