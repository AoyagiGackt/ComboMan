// パーティクルの物理演算と WVP 行列計算を GPU 上で行う Compute Shader

struct GPUParticleState
{
    float3 position; // 12
    float lifeTime; //  4 -> 16
    float3 velocity; // 12
    float currentTime; //  4 -> 32
    float4 color; // 16 -> 48
    float3 scale; // 12
    float rotateZ; //  4 -> 64
    uint alive; //  4
    uint curveFlag; //  4  (1 = enemyDeath 螺旋回転, 2 = flicker)
    float2 pad; //  8 -> 80
};

struct ParticleForGPU
{
    float4x4 WVP;
    float4x4 World;
    float4 color;
    float4 uvRect; // xy=UV のずらし zw=UV の倍率（コマ送り・左右反転）
    float4 params; // x=焼け具合 y=熱さ z=1なら丸く切り抜かない
};

cbuffer UpdateConstants : register(b0)
{
    float4x4 gBillboard; // 64
    float4x4 gViewProj; // 64
    float gDeltaTime; //  4
    uint gMaxParticles; //  4
    float2 gPad; //  8 -> 144
};

static const float kPi = 3.14159265f;
static const float kTwoPi = 6.28318531f;

// 炎（curveFlag == 5）
static const float kFlameBuoyancy = 4.5f; // 上向きに足す加速度
static const float kFlameDrag = 2.5f; // 横の勢いが抜ける速さ
static const float kFlameWobbleHz = 11.0f; // 揺らめきの速さ
static const float kFlameWobble = 3.0f; // 揺らめきの強さ
static const float kFlameSway = 0.18f; // 左右に傾く大きさ（ラジアン）
static const float kFlameSwayHz = 5.0f; // 左右に傾く速さ
static const float kFlameStretch = 0.6f; // 昇る速さに応じて縦へ伸ばす割合
static const float kFlameStretchSpeed = 4.0f; // 伸びきる速さ
static const uint kFlameSheetColumns = 4u; // 炎の画像のコマの並び（横・縦とも）
static const float kFlameErodeStart = 0.04f; // 出た瞬間の焼け具合
static const float kFlameErodeEnd = 0.6f; // 燃え尽きる時の焼け具合
static const float kFlameHeat = 1.6f; // 出た瞬間の熱さ
static const float kFlameHeatKeep = 0.25f; // 燃え尽きる時に残る熱さの割合
static const float kFlameGrowRate = 5.0f; // ふくらみきるまでの速さ（寿命に対する倍率）
static const float kFlameStartScale = 0.5f; // 出た瞬間の大きさの割合
static const float kFlameEndShrink = 0.75f; // 燃え尽きる時に細る割合
static const float kFlameBurnOutStart = 0.45f; // 暗くなり始める時（寿命の割合）
static const float kFlameBurnOutDark = 0.25f; // 燃え尽きた時の明るさ
static const float kFlameStartAlpha = 0.6f; // 出た瞬間の濃さの割合

// 火の粉（curveFlag == 6）
static const float kEmberBuoyancy = 1.5f;
static const float kEmberDrag = 0.8f;
static const float kEmberWobbleHz = 7.0f;
static const float kEmberWobble = 4.0f;
static const float kEmberShrink = 0.7f;
static const float kEmberColorRate = 4.0f; // 白から指定色になる速さ
static const float kEmberTwinkle = 0.4f; // 瞬きで明るさが落ちる割合
static const float kEmberTwinkleHz = 25.0f;

// もや（curveFlag == 7）
static const float kMistDrag = 3.0f;
static const float kMistSpin = 0.8f;
static const float kMistGrow = 1.6f; // 消える時に何倍ぶん大きくなるか
static const float kMistErode = 0.55f; // 消える時の焼け具合（薄い所からほどける）

// 放電（curveFlag == 8）
static const float kCrackleHz = 30.0f; // 位置を跳ばす回数（1秒あたり）
static const float kCrackleJump = 0.35f; // 跳ぶ幅（粒の大きさに対する割合）
static const float kCrackleOnRate = 0.65f; // 点いている確率
static const float kCrackleWhite = 0.7f; // 点いた時に白へ寄せる最大の割合

RWStructuredBuffer<GPUParticleState> gParticles : register(u0);
RWStructuredBuffer<ParticleForGPU> gInstancing : register(u1);

// -------------------------------------------------------
//  xoroshiro128+
// -------------------------------------------------------
uint rotl32u(uint x, int k)
{
    return (x << k) | (x >> (32 - k));
}

uint4 xoshiroSeed(uint seed, uint time, uint idx)
{
    uint a = seed ^ (idx * 2654435761u);
    uint b = time ^ (idx * 2246822519u);
    a ^= a >> 16;
    a *= 0x45d9f3bu;
    a ^= a >> 16;
    b ^= b >> 16;
    b *= 0x45d9f3bu;
    b ^= b >> 16;
    return uint4(a, b, a ^ 0x9e3779b9u, b ^ 0x6c62272eu);
}

uint xoshiroNext(inout uint4 s)
{
    uint result = s.x + s.w;
    uint t = s.y << 9;
    s.z ^= s.x;
    s.w ^= s.y;
    s.y ^= s.z;
    s.x ^= s.w;
    s.z ^= t;
    s.w = rotl32u(s.w, 11);
    return result;
}

float rand01(inout uint4 s)
{
    return float(xoshiroNext(s) >> 8) * (1.0f / 16777216.0f);
}

// -------------------------------------------------------
//  行列ヘルパー
// -------------------------------------------------------
float4x4 MakeScale(float3 s)
{
    return float4x4(
        s.x, 0, 0, 0,
        0, s.y, 0, 0,
        0, 0, s.z, 0,
        0, 0, 0, 1
    );
}

float4x4 MakeRotateZ(float a)
{
    float c = cos(a), s = sin(a);
    return float4x4(
        c, s, 0, 0,
        -s, c, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    );
}

// -------------------------------------------------------
//  groupshared: スレッドグループ内の生存パーティクル数
//  GroupMemoryBarrierWithGroupSync を使うには全スレッドが
//  必ず同じ回数バリアを通過しなければならないため、
//  early return を避けたフラグベースの構造にしている
// -------------------------------------------------------
groupshared uint gs_aliveCount;

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    // --- Sync 1: 共有変数をスレッド 0 が初期化し、全スレッドに同期 ---
    if (GTid.x == 0)
        gs_aliveCount = 0u;
    GroupMemoryBarrierWithGroupSync();

    uint idx = DTid.x;
    bool active = (idx < gMaxParticles);

    GPUParticleState p = (GPUParticleState) 0;
    bool alive = false;

    if (active)
    {
        p = gParticles[idx];
        alive = (p.alive != 0 && p.currentTime < p.lifeTime);

        if (!alive)
        {
            p.alive = 0;
            gParticles[idx] = p;
            gInstancing[idx].color = float4(0, 0, 0, 0);
        }
    }

    // --- 不可分操作: 生存パーティクルをグループ単位でカウント ---
    if (alive)
        InterlockedAdd(gs_aliveCount, 1u);

    // --- Sync 2: 全スレッドの InterlockedAdd 完了を待つ ---
    GroupMemoryBarrierWithGroupSync();

    // gs_aliveCount 確定（このスレッドグループ内の生存数）
    // 現状は使用しないが、将来的な LOD / カリング等に活用可能

    // dead / 範囲外スレッドはここで終了（2 つのバリアはすでに通過済み）
    if (!active || !alive)
        return;

    // --- 生存パーティクルのみ以下を処理 ---

    // curveFlag == 1: enemyDeath 螺旋軌道
    if (p.curveFlag == 1)
    {
        float ca = 5.0f * gDeltaTime;
        float cc = cos(ca), cs = sin(ca);
        float vx = p.velocity.x;
        float vy = p.velocity.y;
        p.velocity.x = vx * cc - vy * cs;
        p.velocity.y = vx * cs + vy * cc;
        p.rotateZ += 12.0f * gDeltaTime;
    }

    // curveFlag == 3: 重力落下
    if (p.curveFlag == 3)
    {
        p.velocity.y -= 15.0f * gDeltaTime;
        p.rotateZ += 8.0f * gDeltaTime;
    }

    // 粒ごとに揺れの位相をずらす（同じ瞬間に出た炎が同じ揺れ方にならないように）
    float phase = float(idx % 13u) * 0.4833f;
    float spinSign = (idx % 2u == 0u) ? 1.0f : -1.0f;

    // curveFlag == 5: 炎 — 浮力で加速しながら昇り、横の勢いは抜け、左右に揺らめく
    if (p.curveFlag == 5)
    {
        p.velocity.y += kFlameBuoyancy * gDeltaTime;
        p.velocity.xz *= saturate(1.0f - kFlameDrag * gDeltaTime);
        p.velocity.x += sin(p.currentTime * kFlameWobbleHz + phase) * kFlameWobble * gDeltaTime;
    }

    // curveFlag == 6: 火の粉 — ゆっくり昇りながら左右へ大きく揺れる
    if (p.curveFlag == 6)
    {
        p.velocity.y += kEmberBuoyancy * gDeltaTime;
        p.velocity.xz *= saturate(1.0f - kEmberDrag * gDeltaTime);
        p.velocity.x += sin(p.currentTime * kEmberWobbleHz + phase) * kEmberWobble * gDeltaTime;
    }

    // curveFlag == 7: もや — 速さが抜けて留まり、ゆっくり回る
    if (p.curveFlag == 7)
    {
        p.velocity *= saturate(1.0f - kMistDrag * gDeltaTime);
        p.rotateZ += kMistSpin * spinSign * gDeltaTime;
    }

    p.position += p.velocity * gDeltaTime;
    p.currentTime += gDeltaTime;
    gParticles[idx] = p;

    float t = saturate(p.currentTime / p.lifeTime);
    float3 renderScale = p.scale;
    float3 renderPos = p.position;
    float renderRot = p.rotateZ;

    // curveFlag == 4: 残像縮小 — 時間とともにスケールを縮める
    if (p.curveFlag == 4)
    {
        renderScale *= 1.0f - t;
    }

    // 炎: 出た直後にふくらみ、燃え尽きるにつれて細る
    float flameGrow = saturate(t * kFlameGrowRate);
    if (p.curveFlag == 5)
    {
        renderScale *= (kFlameStartScale + (1.0f - kFlameStartScale) * flameGrow) * (1.0f - kFlameEndShrink * t * t);
        renderScale.y *= 1.0f + kFlameStretch * saturate(abs(p.velocity.y) / kFlameStretchSpeed);
        renderRot = p.rotateZ + sin(p.currentTime * kFlameSwayHz + phase) * kFlameSway;
    }

    // 火の粉・もやの大きさ
    if (p.curveFlag == 6)
    {
        renderScale *= 1.0f - kEmberShrink * t;
    }
    if (p.curveFlag == 7)
    {
        renderScale *= 1.0f + kMistGrow * t;
    }

    // 放電: 一定間隔ごとに位置と向きを跳ばす（積み上げずに描く時だけずらす）
    uint crackleSlot = (uint) (p.currentTime * kCrackleHz);
    uint4 crackleRng = xoshiroSeed(crackleSlot, idx * 2654435761u, idx);
    if (p.curveFlag == 8)
    {
        float jump = kCrackleJump * max(p.scale.x, p.scale.y);
        renderPos.x += (rand01(crackleRng) * 2.0f - 1.0f) * jump;
        renderPos.y += (rand01(crackleRng) * 2.0f - 1.0f) * jump;
        renderRot = rand01(crackleRng) * kTwoPi;
    }

    // ワールド行列: scale * rotateZ * billboard、row3 に平行移動を直接書く
    float4x4 world = mul(mul(MakeScale(renderScale), MakeRotateZ(renderRot)), gBillboard);
    world._m30 = renderPos.x;
    world._m31 = renderPos.y;
    world._m32 = renderPos.z;
    world._m33 = 1.0f;

    gInstancing[idx].WVP = mul(world, gViewProj);
    gInstancing[idx].World = world;
    gInstancing[idx].uvRect = float4(0.0f, 0.0f, 1.0f, 1.0f);
    gInstancing[idx].params = float4(0.0f, 0.0f, 0.0f, 0.0f);

    // 炎: 寿命に合わせて画像のコマを進め、粒ごとに左右を反転して同じ形が並ばないようにする
    if (p.curveFlag == 5)
    {
        uint frameCount = kFlameSheetColumns * kFlameSheetColumns;
        uint frame = min((uint) (t * float(frameCount)), frameCount - 1u);
        float cell = 1.0f / float(kFlameSheetColumns);
        float2 origin = float2(float(frame % kFlameSheetColumns), float(frame / kFlameSheetColumns)) * cell;
        bool mirror = (idx % 2u) == 1u;
        gInstancing[idx].uvRect = float4(mirror ? origin.x + cell : origin.x, origin.y, mirror ? -cell : cell, cell);
        gInstancing[idx].params = float4(lerp(kFlameErodeStart, kFlameErodeEnd, t),
            kFlameHeat * (1.0f - (1.0f - kFlameHeatKeep) * t), 1.0f, 0.0f);
    }
    // もや: 薄い所からほどけて消える
    if (p.curveFlag == 7)
    {
        gInstancing[idx].params = float4(kMistErode * t, 0.0f, 0.0f, 0.0f);
    }

    // curveFlag == 2: xoroshiro128+ でパーティクルごとに独立した周期でランダム点滅
    if (p.curveFlag == 2)
    {
        float flickerHz = 2.0f + float(idx % 7u) * 0.857142857f; // 2〜8 Hz でばらつかせる
        uint timeSlot = (uint) (p.currentTime * flickerHz);
        uint4 rng = xoshiroSeed(timeSlot, idx * 3141592653u, idx);
        float r = rand01(rng);
        gInstancing[idx].color = float4(p.color.rgb, p.color.a * (r < 0.75f ? 1.0f : 0.0f));
    }
    else if (p.curveFlag == 5)
    {
        // 指定色から暗く燃え尽きる（白く熱い芯はピクセルシェーダーが濃さから付ける）。消え方は焼け具合に任せる
        float3 rgb = lerp(p.color.rgb, p.color.rgb * kFlameBurnOutDark, saturate((t - kFlameBurnOutStart) / (1.0f - kFlameBurnOutStart)));
        float alpha = p.color.a * (1.0f - t * t * t) * (kFlameStartAlpha + (1.0f - kFlameStartAlpha) * flameGrow);
        gInstancing[idx].color = float4(rgb, alpha);
    }
    else if (p.curveFlag == 6)
    {
        // 出た直後は白く、すぐ指定色になり、細かく瞬く
        float3 rgb = lerp(float3(1.0f, 1.0f, 1.0f), p.color.rgb, saturate(t * kEmberColorRate));
        float twinkle = 1.0f - kEmberTwinkle + kEmberTwinkle * sin(p.currentTime * kEmberTwinkleHz + phase * 3.0f);
        gInstancing[idx].color = float4(rgb, p.color.a * (1.0f - t) * twinkle);
    }
    else if (p.curveFlag == 7)
    {
        // ふわっと現れてふわっと消える
        gInstancing[idx].color = float4(p.color.rgb, p.color.a * sin(t * kPi));
    }
    else if (p.curveFlag == 8)
    {
        // 間隔ごとに点いたり消えたりし、点いた時は白へ寄せる
        bool lit = rand01(crackleRng) < kCrackleOnRate;
        float3 rgb = lerp(p.color.rgb, float3(1.0f, 1.0f, 1.0f), rand01(crackleRng) * kCrackleWhite);
        gInstancing[idx].color = float4(rgb, lit ? p.color.a * (1.0f - t * t) : 0.0f);
    }
    else
    {
        float alpha = 1.0f - (p.currentTime / p.lifeTime);
        gInstancing[idx].color = float4(p.color.rgb, p.color.a * alpha);
    }
}
