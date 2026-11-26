/**
 * @file ParticleManager.h
 * @brief GPU パーティクル（粒の生き死にと空き枠の管理まで GPU で完結）をインスタンシング描画するファイル
 * @note CPU はどこに何粒出してほしいかの一覧だけをフレームごとに送る。
 *       空き枠は GPU 上の空き一覧（InterlockedAdd で取り出し・返却）で管理し、寿命の判定も GPU が行う
 */
#pragma once
#include "Camera.h"
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include "Model.h"
#include "SrvManager.h"
#include <array>
#include <d3d12.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>
namespace engine::graphics {

/**
 * @brief 粒子の動き方（ParticleUpdate.CS.hlsl の curveFlag と同じ番号）
 */
enum class ParticleMotion : uint32_t {
    kLinear = 0, ///< 初速のまま進む
    kSpiral = 1, ///< 螺旋を描く
    kFlicker = 2, ///< 明滅する
    kGravity = 3, ///< 重力で落ちる
    kShrinkFade = 4, ///< 縮みながら消える（残像）
    kFlame = 5, ///< 浮力で昇りながら揺れ、白く熱い芯から指定色を経て暗く燃え尽きる
    kEmber = 6, ///< 小さく揺れながら昇り、瞬きながら縮む
    kMist = 7, ///< 速さが抜けながら膨らみ、ふわっと現れて消える
    kCrackle = 8, ///< その場で小刻みに跳ねて明滅する（放電）
    kSoul = 9, ///< 加速しながら揺れて昇り、白く抜け出してふわっと消える（倒れた敵から抜ける魂）
    kGoo = 10, ///< 重く落ち、飛ぶ向きへ伸びて細る（粘液のしぶき）
    kFlutter = 11, ///< ゆっくり落ちながら左右に揺れ、くるくる裏返る（羽や骨のかけら）
};

/**
 * @brief GPU 側で保持するパーティクル 1 粒のシミュレーションデータ（ParticleCompute.hlsli と同じ並び）
 * @note 出してほしい粒の頼みも同じ形で送る
 */
struct GPUParticleState {
    Vector3 position; // 12
    float lifeTime; //  4 -> 16
    Vector3 velocity; // 12
    float currentTime; //  4 -> 32
    Vector4 color; // 16 -> 48
    Vector3 scale; // 12
    float rotateZ; //  4 -> 64
    uint32_t alive; //  4
    uint32_t curveFlag; //  4  ParticleMotion の番号
    float pad[2]; //  8 -> 80
};

/**
 * @brief 描画のために VS が読むインスタンシングデータ (CS が書き込み)
 */
struct ParticleForGPU {
    Matrix4x4 wvp;
    Matrix4x4 world;
    Vector4 color;
    Vector4 uvRect; ///< xy=UV のずらし zw=UV の倍率（炎のコマ送り・左右反転）
    Vector4 params; ///< x=焼け具合 y=熱さ z=1なら丸く切り抜かない
};

/**
 * @brief 更新の CS に渡す定数（全グループ共通）
 */
struct CSConstants {
    Matrix4x4 billboard; // 64
    Matrix4x4 viewProj; // 64
    float deltaTime; //  4
    uint32_t maxParticles; //  4
    uint32_t frame; //  4  フレームの通し番号（生まれ変わりの乱数の種）
    float pad; //  4 -> 144
};

/**
 * @brief EmitParticle.CS に渡すエミッターの定数
 *        CPU 側で frequencyTime / emit を毎フレーム更新し、出す時だけ GPU へ送る
 */
struct Emitter {
    Vector3 translate; // 12
    float radius; //  4 -> 16
    uint32_t count; //  4
    float frequency; //  4
    float frequencyTime; //  4
    uint32_t emit; //  4 -> 32
    float lifeTime; //  4
    uint32_t seed; //  4
    uint32_t time; //  4  (groupTime の float ビット列、Update が自動設定)
    float pad; //  4 -> 48
    Vector4 color; // 16 -> 64
};

/**
 * @brief 寿命が尽きた粒を同じ範囲に GPU で出し直すグループの設定（ParticleUpdate.CS.hlsl の LoopConstants と同じ並び）
 */
struct ParticleLoopConfig {
    Vector3 center {}; ///< 中心
    float radius = 0.0f; ///< 横と奥行きに広がる半径
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f }; ///< 色
    float lifeTimeMin = 1.0f; ///< 寿命の幅
    float lifeTimeMax = 1.0f;
    float scale = 1.0f; ///< 大きさ
    uint32_t enabled = 0; ///< 1 なら出し直す
    float height = 0.0f; ///< 中心から上へ広がる高さ
    float driftXZ = 0.0f; ///< 横に流れる速さの幅
    float riseMin = 0.0f; ///< 昇る速さの幅
    float riseMax = 0.0f;
    uint32_t seed = 0; ///< グループごとの乱数の種
    float fieldScale = 1.0f; ///< 場（ParticleField）の効き方の倍率（0 で効かない。斬撃の線や武器の軌跡は 0 にする）
    float pad[2] = {};
};

/** @brief 場の種類（ParticleUpdate.CS.hlsl と同じ番号） */
enum class ParticleFieldType : uint32_t {
    kRadial = 0, ///< 中心から押し広げる・引き寄せる（strength が正で押す、負で引く。swirl で渦を巻かせる）
    kDirectional = 1, ///< 決まった向きへ流す（風・引きずり）
};

/**
 * @brief 範囲の中の粒に力を掛ける場（ParticleUpdate.CS.hlsl の ParticleField と同じ並び。48 バイト）
 * @note 中心から離れるほど弱まり、半径の外では効かない
 */
struct ParticleField {
    Vector3 center {}; ///< 中心
    float radius = 1.0f; ///< 効く半径
    Vector3 direction = { 1.0f, 0.0f, 0.0f }; ///< kDirectional の流す向き（長さ1）
    float strength = 0.0f; ///< 加速度（kRadial は正で押し広げ、負で引き寄せる）
    float swirl = 0.0f; ///< 中心のまわりを回す加速度（画面の奥行きの軸まわり。正で反時計回り）
    float drag = 0.0f; ///< 範囲の中で速さを抜く強さ（引き寄せた粒が中心を行き過ぎないように）
    ParticleFieldType type = ParticleFieldType::kRadial;
    float pad = 0.0f;
};

/**
 * @brief ソフトパーティクルの定数（Particle.PS.hlsl の SoftParticleConstants と同じ並び）
 */
struct SoftParticleConstants {
    float depthA = 0.0f; ///< 射影行列の m[2][2]
    float depthB = 0.0f; ///< 射影行列の m[3][2]
    float softRange = 1.0f; ///< 後ろの物にこの奥行きまで近づくと薄くなり始める
    uint32_t enabled = 0; ///< 1 ならソフトパーティクルにする
};

// シェーダーと同じ並び・大きさであることを確かめる（HLSL 側を変えたら一緒に変える）
static_assert(sizeof(GPUParticleState) == 80, "GPUParticleState は ParticleCompute.hlsli と同じ 80 バイト");
static_assert(sizeof(ParticleForGPU) == 176, "ParticleForGPU は ParticleCompute.hlsli と Particle.VS.hlsl と同じ 176 バイト");
static_assert(sizeof(CSConstants) == 144, "CSConstants は ParticleUpdate.CS.hlsl の UpdateConstants と同じ 144 バイト");
static_assert(sizeof(Emitter) == 64, "Emitter は EmitParticle.CS.hlsl の CBEmitter と同じ 64 バイト");
static_assert(sizeof(ParticleLoopConfig) == 80, "ParticleLoopConfig は ParticleUpdate.CS.hlsl の LoopConstants と同じ 80 バイト");
static_assert(sizeof(ParticleField) == 48, "ParticleField は ParticleUpdate.CS.hlsl の ParticleField と同じ 48 バイト");
static_assert(sizeof(SoftParticleConstants) == 16, "SoftParticleConstants は Particle.PS.hlsl と同じ 16 バイト");

/**
 * @brief 同じテクスチャを共有するパーティクルの集まり（グループ）
 */
struct ParticleGroup {
    std::string textureFilePath;

    static constexpr uint32_t kNumMaxInstance = 8192;

    // GPU 上の粒の状態・空き一覧・空きの数（DEFAULT heap, UAV）
    Microsoft::WRL::ComPtr<ID3D12Resource> particleStateBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> counterBuffer;

    // インスタンシング描画バッファ (DEFAULT heap, CS が UAV として書き、VS が SRV として読む)
    uint32_t srvIndex = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource;

    // 生きている粒の番号の一覧と、その数だけ描く間接描画の引数（ParticleUpdate.CS が一覧を、ParticleBuildArgs.CS が引数を書く）
    Microsoft::WRL::ComPtr<ID3D12Resource> drawListBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> drawArgsBuffer;
    // GPU が数えた生きている粒の数を CPU へ写す先（画像の番号ごとに1つ。数フレーム遅れの値を表示だけに使う）
    Microsoft::WRL::ComPtr<ID3D12Resource> aliveCountReadback;
    const uint32_t* aliveCountMapped = nullptr; ///< 写す先を開いたままにした場所
    uint32_t gpuAliveCount = 0; ///< 読み戻した生きている粒の数

    float groupTime = 0.0f;
    // CPU が知っている、最後の粒が消える時刻の見込み（GPU から読み戻さずに、描く・更新するかを決める）
    float aliveUntil = 0.0f;

    // このフレームに出してほしい粒の一覧（次の Update で GPU へ送る）
    std::vector<GPUParticleState> pendingSpawns;

    bool instancingInSRV = false; // false=UAV  true=描く側の状態（描画用と描く一覧は NON_PIXEL_SHADER_RESOURCE、引数は INDIRECT_ARGUMENT）
    bool needsReset = true; // 次の Update で全部の粒を消し、空き一覧を詰め直す
    bool buffersFresh = true; // 作成直後の COMMON 状態か（最初の更新で UAV へ移す）
    bool additiveBlend = true; // false = alpha blend (SRC_ALPHA / INV_SRC_ALPHA)

    // GPU エミッター (EmitParticle.CS)
    Emitter emitter {};

    // 寿命が尽きた粒を GPU で出し直す設定（EmitScatterLoop 用）
    ParticleLoopConfig loop {};
};

/**
 * @brief パーティクル全体を管理し、CS でシミュレーション・インスタンシング描画を行うシングルトン
 */
class ParticleManager {
public:
    /** @brief ParticleManager の唯一のインスタンスを取得する */
    static ParticleManager* GetInstance();

    /**
     * @brief GPU リソース・パイプラインを作成して初期化する
     * @param dxCommon デバイス・コマンドリスト取得に使用する DirectX 基盤
     */
    void Initialize(engine::DirectXCommon* dxCommon);

    /** @brief 全グループを破棄し GPU リソースを解放する */
    void Finalize();

    /**
     * @brief 全グループの Compute Shader をディスパッチしてパーティクルを更新する
     * @param camera ビルボード行列・VP 行列の計算に使用するカメラ
     */
    void Update(Camera* camera);

    /**
     * @brief 生存中のパーティクルをインスタンシング描画する
     * @param camera ソフトパーティクルで深度から奥行きへ戻すのに使うカメラ（nullptr ならソフトにしない）
     */
    void Draw(Camera* camera);

    /**
     * @brief 白色・デフォルトパラメーターで1粒放出する
     * @param name     対象グループ名
     * @param position 放出位置
     * @param velocity 初速ベクトル
     */
    void Emit(const std::string& name, const Vector3& position, const Vector3& velocity);

    /**
     * @brief 色・寿命・スケールを指定して1粒放出する
     * @param flicker true にすると明滅エフェクト（curveFlag=2）を適用する
     */
    void EmitWithColor(const std::string& name, const Vector3& position,
        const Vector3& velocity, const Vector4& color,
        float lifeTime = 1.0f, float scale = 1.0f, bool flicker = false);

    /**
     * @brief 動き方と向きを指定して1粒放出する（炎・火の粉・もや・放電など）
     * @param motion  粒の動き方
     * @param rotateZ 粒の向き（ラジアン）
     */
    void EmitMotion(const std::string& name, const Vector3& position,
        const Vector3& velocity, const Vector4& color,
        float lifeTime, float scale, ParticleMotion motion, float rotateZ = 0.0f);

    /**
     * @brief 横長の楕円形スケールで1粒放出する（斬撃の残光等に使用）
     */
    void EmitEllipse(const std::string& name, const Vector3& position,
        const Vector3& velocity, const Vector4& color,
        float lifeTime = 1.0f, float scaleX = 2.0f, float scaleY = 1.0f);

    /**
     * @brief 斬撃の剣閃（残光＋芯＋斬線に沿って抜ける光片）を放出する
     * @param angle  斬撃の角度（ラジアン）
     * @param radius 斬線の半長（ワールド単位）
     */
    void EmitSlash(const std::string& name, const Vector3& position,
        float angle, const Vector4& color, float radius = 1.0f);

    /**
     * @brief ヒット時の星形エフェクト用にランダム方向・スケールで複数粒放出する
     */
    void EmitHitStar(const std::string& name, const Vector3& position, const Vector4& color);

    /**
     * @brief グループを空にしてから、指定数のパーティクルをランダム位置に一斉配置する（環境パーティクル等）
     * @param count    放出数（最大 kNumMaxInstance）
     * @param lifeTime 各パーティクルの寿命（デフォルトは実質無限）
     */
    void EmitBurst(const std::string& name, const Vector3& position, const Vector4& color,
        uint32_t count = ParticleGroup::kNumMaxInstance,
        float lifeTime = kDefaultBurstLifetime, float scale = 1.0f, bool flicker = false);

    /**
     * @brief 指定半径・高さにランダム配置し、寿命が尽きると GPU で再配置するループエフェクトを開始する
     */
    void EmitScatterLoop(const std::string& name, const Vector3& center, float radius,
        uint32_t count, const Vector4& color,
        float lifeTimeMin, float lifeTimeMax, float scale);

    /**
     * @brief 初速を与えて重力落下するパーティクルを1粒放出する（curveFlag=3）
     */
    void EmitGravity(const std::string& name, const Vector3& position,
        const Vector3& velocity, const Vector4& color,
        float lifeTime = kDefaultGravityLifetime, float scale = kDefaultGravityScale);

    /**
     * @brief 均等角度で放射状にリングエフェクトを放出する（ヒットストップ等）
     */
    void EmitRing(const std::string& name, const Vector3& position,
        float speed, const Vector4& color,
        uint32_t count = kDefaultRingCount, float lifeTime = kDefaultRingLifetime, float scale = kDefaultRingScale);

    /**
     * @brief 残像1粒を放出する毎フレーム呼び続けることでトレイルを形成する
     */
    void EmitTrail(const std::string& name, const Vector3& position,
        const Vector4& color, float scale = 0.5f, float lifeTime = kDefaultTrailLifetime);

    /**
     * @brief 指定グループのテクスチャを差し替える
     * @param groupName       変更対象のグループ名
     * @param textureFilePath 新しいテクスチャのファイルパス
     */
    void SetTexture(const std::string& groupName, const std::string& textureFilePath);

    /**
     * @brief 新しいパーティクルグループを生成・登録する
     * @param name            グループの識別名（重複不可）
     * @param textureFilePath 使用テクスチャのファイルパス
     */
    void CreateParticleGroup(const std::string& name, const std::string& textureFilePath);

    /** @brief 全パーティクルグループを破棄する */
    void ClearAllGroups() {
        particleGroups_.clear();
    }

    /**
     * @brief グループの加算合成・アルファ合成を切り替える
     * @param additive true=加算合成, false=アルファ合成
     */
    void SetAdditiveBlend(const std::string& name, bool additive);

    /**
     * @brief 指定グループに生存中のパーティクルが残っていそうかを返す（GPU から読み戻さず、CPU の見込みで答える）
     * @return true=生存あり / false=全滅
     */
    bool IsGroupAlive(const std::string& name) const;

    /**
     * @brief 指定グループのエミッターデータへのポインタを返す
     * @note 呼び出し側で translate/radius/count/frequency/lifeTime を直接設定する
     */
    Emitter* GetEmitter(const std::string& name);

    /** @brief ソフトパーティクルにするか（既定は有効） */
    void SetSoftParticlesEnabled(bool enabled) {
        softParticlesEnabled_ = enabled;
    }

    /** @brief GPU が数えた、全グループの生きている粒の数（数フレーム前の値。診断の表示用） */
    uint32_t GetGpuAliveParticleCount() const;

    /** @brief 1フレームに効かせられる場の数 */
    static constexpr uint32_t kMaxFields = 16;
    /**
     * @brief 粒に力を掛ける場を置く（全グループの粒に効く。グループごとの効き方は SetFieldScale で変える）
     * @param field   場
     * @param seconds 0 ならこのフレームだけ（続けて効かせる時は毎フレーム置く）。正ならその秒数だけ残り、だんだん弱まる
     */
    void AddField(const ParticleField& field, float seconds = 0.0f);
    /** @brief グループの粒への場の効き方の倍率を決める（0 で効かない） */
    void SetFieldScale(const std::string& name, float scale);
    /** @brief 置いてある場をすべて消す（シーンを抜ける時） */
    void ClearFields() {
        timedFields_.clear();
        frameFields_.clear();
    }

private:
    /** @brief 更新の CS に渡す場の一覧（ParticleUpdate.CS.hlsl の FieldConstants と同じ並び） */
    struct FieldConstants {
        uint32_t count = 0;
        float pad[3] = {};
        std::array<ParticleField, kMaxFields> fields {};
    };
    static_assert(sizeof(FieldConstants) == 16 + 48 * kMaxFields, "FieldConstants は ParticleUpdate.CS.hlsl の FieldConstants と同じ並び");
    /** @brief 秒数を決めて置いた場 */
    struct TimedField {
        ParticleField field;
        float remaining = 0.0f; ///< 残りの秒数
        float total = 0.0f; ///< 置いた時の秒数（弱まり方に使う）
    };
    /** @brief このフレームに効かせる場を集めて今のフレームの領域へ書き、秒数を決めた場の時間を進める */
    D3D12_GPU_VIRTUAL_ADDRESS UploadFields(float dt);

    // 放出APIの既定値
    static constexpr float kDefaultBurstLifetime = 100000.0f;
    static constexpr float kDefaultGravityLifetime = 0.6f;
    static constexpr float kDefaultGravityScale = 0.2f;
    static constexpr uint32_t kDefaultRingCount = 16;
    static constexpr float kDefaultRingLifetime = 0.4f;
    static constexpr float kDefaultRingScale = 0.3f;
    static constexpr float kDefaultTrailLifetime = 0.15f;

    /** @brief 粒子とGPU描画資源が未登録の管理器を作る */
    ParticleManager() = default;
    /** @brief 所有する資源はメンバのデストラクタで解放する */
    ~ParticleManager() = default;
    /** @brief GPU 資源を二重に持たないようにコピーを禁じる */
    ParticleManager(const ParticleManager&) = delete;
    ParticleManager& operator=(const ParticleManager&) = delete;

    // パイプライン構築
    /** @brief 描画と計算用シェーダーのルートシグネチャを作る */
    void CreateRootSignature();
    /** @brief 粒を描くパイプラインを作る */
    void CreatePipelineState();
    /** @brief 初期化・出現・発射・更新の計算用パイプラインを作る */
    void CreateComputePipelines();
    /** @brief 粒1枚ぶんの四角形の頂点を作る */
    void CreateQuadGeometry();

    // 環境のばらまきループ（EmitScatterLoop と GPU での出し直しで共通）の配置範囲と漂い方
    static constexpr float kScatterHeight = 12.0f;
    static constexpr float kScatterDriftXZ = 0.3f;
    static constexpr float kScatterRiseMin = 0.1f;
    static constexpr float kScatterRiseMax = 0.6f;

    /** @brief Emit系で1粒を出す時の中身 */
    struct ParticleSpawn {
        Vector3 position;
        Vector3 velocity;
        Vector4 color;
        Vector3 scale;
        float lifeTime = 0.0f;
        float rotateZ = 0.0f;
        ParticleMotion motion = ParticleMotion::kLinear;
        float currentTime = 0.0f; ///< 寿命の途中から始める時の経過時間（ふつうは 0）
    };
    /** @brief 出してほしい粒の一覧へ1粒足す（1フレームに頼める数を超えたら何もせず false） */
    static bool SpawnParticle(ParticleGroup& group, const ParticleSpawn& spawn);
    /** @brief グループの粒を次の更新ですべて消すように頼む（まだ送っていない頼みも捨てる） */
    static void RequestReset(ParticleGroup& group);

    // CreateParticleGroup のフェーズ分割ヘルパー
    /** @brief 粒の状態・空き一覧・空きの数のバッファを作る */
    void CreateParticleBuffers(ParticleGroup& group);
    /** @brief 描画で読むインスタンスのバッファと SRV・UAV を作る */
    void CreateParticleInstancingResource(ParticleGroup& group);

    /** @brief グループを更新する必要があるか（生きている粒・頼み・エミッター・出し直しのどれかがある） */
    static bool NeedsUpdate(const ParticleGroup& group);
    /** @brief グループの粒を描く必要があるか */
    static bool NeedsDraw(const ParticleGroup& group);

    // Update のフェーズ分割ヘルパー
    /** @brief 更新の CS の定数を作り、今のフレームの領域へ書いてアドレスを返す */
    D3D12_GPU_VIRTUAL_ADDRESS UploadCSConstants(Camera* camera, float dt);
    /** @brief 描画で読んでいたバッファを CS が書ける状態へ戻す */
    void TransitionInstancingToUAV(ParticleGroup& group, ID3D12GraphicsCommandList* cmd);
    /** @brief 作成直後のバッファを UAV の状態へ移す */
    static void TransitionBuffersToUAV(ParticleGroup& group, ID3D12GraphicsCommandList* cmd);
    /** @brief 全部の粒を消し、空き一覧に全部の枠を積む */
    void DispatchInitCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd);
    /** @brief 出してほしい粒の一覧を送り、空き一覧から枠を取って書き込む */
    void DispatchSpawnCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd);
    /**
     * @brief 発生頻度に応じて発射タイミングを判定し、来ていれば発射用コンピュートシェーダを
     *        ディスパッチして空き一覧から枠を取って新規パーティクルを生成する（発射タイミングでなければ何もしない）
     * @param group 対象パーティクルグループ
     * @param cmd   ディスパッチを積むコマンドリスト
     * @param dt    経過時間（発生間隔タイマーの加算に使う）
     */
    void DispatchEmitCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd, float dt);
    /** @brief 生きている粒を動かし、寿命の尽きた粒の枠を空き一覧へ返す */
    void DispatchUpdateCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS constants, D3D12_GPU_VIRTUAL_ADDRESS fields);
    /** @brief 生きている粒の数から間接描画の引数を作り、数を CPU へ写す（数は次のフレームのために 0 へ戻す） */
    void DispatchBuildDrawArgsCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd);
    /** @brief 生きている粒の一覧・間接描画の引数・数を読み戻す先を作る */
    void CreateDrawListResources(ParticleGroup& group);
    /** @brief 間接描画（インデックス付きの描画1回）の命令の形を作る */
    void CreateDrawCommandSignature();
    /** @brief 粒の状態・空き一覧・空きの数の書き込みが終わるのを待つ（次の CS が読む前に挟む） */
    static void UavBarrier(ID3D12GraphicsCommandList* cmd);
    /** @brief 今のフレームの領域へ書いてアドレスを返す */
    D3D12_GPU_VIRTUAL_ADDRESS UploadFrameData(const void* data, size_t size);

    // ソフトパーティクル
    /** @brief 写し取った深度1枚（写し取る元の深度バッファごとに持つ） */
    struct SceneDepthCopy {
        ID3D12Resource* source = nullptr; ///< 写し取る元（所有しない）
        UINT64 width = 0; ///< 作った時の元の大きさ（変わったら作り直す）
        UINT height = 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> texture; ///< 写し先
        uint32_t srvIndex = UINT32_MAX; ///< 写し先を読む SRV
        bool readable = false; ///< 写し先が読める状態か（false ならコピー先の状態）
    };
    static constexpr size_t kSceneDepthCopyCount = 2; ///< バックバッファの深度とオフスクリーンの深度
    /**
     * @brief 今描画先にしている深度を写し取り、その SRV を返す（写し取れなければ nullptr）
     * @note 描画先の深度はそのまま深度テストに使い続けるので、読むのは写しの方
     */
    const SceneDepthCopy* CopySceneDepth(ID3D12GraphicsCommandList* cmd);
    /** @brief 元の深度バッファに合う写し先を用意する（大きさが変わっていたら作り直す） */
    SceneDepthCopy* PrepareSceneDepthCopy(ID3D12Resource* source);

private:
    engine::DirectXCommon* dxCommon_ = nullptr;

    // quad ジオメトリ（全グループ共有）
    Microsoft::WRL::ComPtr<ID3D12Resource> quadVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> quadIndexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW quadVBV_ { };
    D3D12_INDEX_BUFFER_VIEW quadIBV_ { };

    // グラフィックスパイプライン
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_; // additive blend
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateAlpha_; // alpha blend

    /** @brief 計算用シェーダー1つ分のルートシグネチャとパイプライン */
    struct ComputePass {
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    };
    ComputePass initPass_; ///< ParticleInit.CS（グループを空にする）
    ComputePass spawnPass_; ///< ParticleSpawn.CS（頼まれた粒を出す）
    ComputePass emitPass_; ///< EmitParticle.CS（エミッター）
    ComputePass updatePass_; ///< ParticleUpdate.CS（動かす・寿命の判定・空き一覧へ返す）
    ComputePass buildArgsPass_; ///< ParticleBuildArgs.CS（生きている粒の数から間接描画の引数を作る）
    /** @brief 間接描画の命令の形（インデックス付きの描画1回だけ。ルートの値は変えない） */
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> drawCommandSignature_;

    std::unordered_map<std::string, ParticleGroup> particleGroups_;
    uint32_t frameCounter_ = 0; ///< 更新の通し番号（GPU での出し直しの乱数の種）

    std::array<SceneDepthCopy, kSceneDepthCopyCount> sceneDepthCopies_ {};
    bool softParticlesEnabled_ = true;

    std::vector<TimedField> timedFields_; ///< 秒数を決めて置いた場
    std::vector<ParticleField> frameFields_; ///< このフレームだけ効かせる場
};

} // namespace engine::graphics
