// パーティクルの物理演算と WVP 行列計算を GPU 上で行う Compute Shader
// 寿命が尽きた粒はその場で空き一覧へ枠を返す（出し直すグループは、その場で新しい粒に生まれ変わらせる）
#include "ParticleCompute.hlsli"

cbuffer UpdateConstants : register(b0)
{
    float4x4 gBillboard; // 64
    float4x4 gViewProj; // 64
    float gDeltaTime; //  4
    uint gMaxParticles; //  4
    uint gFrame; //  4  フレームの通し番号（生まれ変わりの乱数の種）
    float gPad; //  4 -> 144
};

// 寿命が尽きた粒を同じ範囲に出し直すグループの設定（C++ の ParticleLoopConfig と同じ並び）
cbuffer LoopConstants : register(b1)
{
    float3 gLoopCenter; // 中心
    float gLoopRadius; // 横と奥行きに広がる半径
    float4 gLoopColor; // 色
    float gLoopLifeTimeMin; // 寿命の幅
    float gLoopLifeTimeMax;
    float gLoopScale; // 大きさ
    uint gLoopEnabled; // 1 なら出し直す
    float gLoopHeight; // 中心から上へ広がる高さ
    float gLoopDriftXZ; // 横に流れる速さの幅
    float gLoopRiseMin; // 昇る速さの幅
    float gLoopRiseMax;
    uint gLoopSeed; // グループごとの乱数の種
    float gFieldScale; // 場の効き方の倍率（0 で効かない）
    float2 gLoopPad;
};

// 範囲の中の粒に力を掛ける場（C++ の ParticleField と同じ並び。48 バイト）
struct ParticleField
{
    float3 center; // 中心
    float radius; // 効く半径
    float3 direction; // kFieldDirectional の流す向き
    float strength; // 加速度（放射の場は正で押し広げ、負で引き寄せる）
    float swirl; // 中心のまわりを回す加速度
    float drag; // 範囲の中で速さを抜く強さ
    uint type; // 0=放射 1=一方向
    float pad;
};

static const uint kMaxFields = 16u; // C++ の ParticleManager::kMaxFields と同じ
static const uint kFieldDirectional = 1u;
static const float kFieldMinDistance = 0.0001f; // 中心にぴったり重なった粒は上へ押す

// このフレームに効かせる場（全グループで同じもの）
cbuffer FieldConstants : register(b2)
{
    uint gFieldCount;
    float3 gFieldPad;
    ParticleField gFields[kMaxFields];
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

// 魂（curveFlag == 9）— 倒れた敵から抜け出して、揺れながら昇っていく
static const float kSoulRise = 2.2f; // 上向きに足す加速度
static const float kSoulDrag = 1.5f; // 横の勢いが抜ける速さ
static const float kSoulSwayHz = 3.0f; // 左右に揺れる速さ
static const float kSoulSway = 2.5f; // 左右に揺れる強さ
static const float kSoulGrow = 0.6f; // 消えるまでにふくらむ割合
static const float kSoulWhiten = 0.5f; // 出た直後に白へ寄せる割合

// 粘液（curveFlag == 10）— 重く落ち、飛ぶ向きへ伸びて潰れる
static const float kGooGravity = 18.0f; // 落ちる加速度
static const float kGooStretch = 0.9f; // 速さに応じて飛ぶ向きへ伸ばす割合
static const float kGooStretchSpeed = 6.0f; // 伸びきる速さ
static const float kGooMinThin = 0.55f; // 伸びた時に細る下限（伸びた分だけ細くして量を保つ）
static const float kGooFadeStart = 0.6f; // 薄れ始める時（寿命の割合）

// 舞い落ちる破片（curveFlag == 11）— ゆっくり落ちながら左右に揺れ、くるくる裏返る
static const float kFlutterGravity = 2.5f; // 落ちる加速度
static const float kFlutterDrag = 2.2f; // 速さが抜ける速さ
static const float kFlutterSwayHz = 4.0f; // 左右に揺れる速さ
static const float kFlutterSway = 5.0f; // 左右に揺れる強さ
static const float kFlutterSpin = 7.0f; // 回る速さ（ラジアン毎秒）
static const float kFlutterFlipHz = 6.0f; // 裏返る速さ
static const float kFlutterMinWidth = 0.15f; // 裏返って細く見える時の幅の下限

RWStructuredBuffer<GPUParticleState> gParticles : register(u0);
RWStructuredBuffer<ParticleForGPU> gInstancing : register(u1);
RWStructuredBuffer<uint> gFreeList : register(u2);
RWStructuredBuffer<int> gCounter : register(u3);

// 場の力で速さを変える（中心から離れるほど弱く、半径の外では効かない）
float3 ApplyFields(float3 position, float3 velocity)
{
    for (uint i = 0u; i < gFieldCount; ++i)
    {
        ParticleField field = gFields[i];
        float3 offset = position - field.center;
        float dist = length(offset);
        if (dist >= field.radius)
        {
            continue;
        }
        float falloff = (1.0f - dist / field.radius) * gFieldScale;
        float3 accel;
        if (field.type == kFieldDirectional)
        {
            accel = field.direction * field.strength;
        }
        else
        {
            float3 outward = dist > kFieldMinDistance ? offset / dist : float3(0.0f, 1.0f, 0.0f);
            // 押し広げる・引き寄せる力に、画面の奥行きの軸まわりに回す力を足して渦にする
            float3 tangent = float3(-outward.y, outward.x, 0.0f);
            accel = outward * field.strength + tangent * field.swirl;
        }
        velocity += accel * falloff * gDeltaTime;
        velocity *= saturate(1.0f - field.drag * falloff * gDeltaTime);
    }
    return velocity;
}

// 出し直すグループで、寿命が尽きた枠に新しい粒を作る（中心のまわりの円柱の中に置き、横に流れながら昇る）
GPUParticleState RespawnLoopParticle(uint idx)
{
    uint4 rng = XoshiroSeed(gLoopSeed, gFrame, idx);
    GPUParticleState p = (GPUParticleState) 0;
    p.position = gLoopCenter + float3(RandRange(rng, -gLoopRadius, gLoopRadius),
        Rand01(rng) * gLoopHeight, RandRange(rng, -gLoopRadius, gLoopRadius));
    p.lifeTime = RandRange(rng, gLoopLifeTimeMin, gLoopLifeTimeMax);
    p.velocity = float3(RandRange(rng, -gLoopDriftXZ, gLoopDriftXZ),
        RandRange(rng, gLoopRiseMin, gLoopRiseMax), RandRange(rng, -gLoopDriftXZ, gLoopDriftXZ));
    p.color = gLoopColor;
    p.scale = float3(gLoopScale, gLoopScale, gLoopScale);
    p.alive = 1u;
    return p;
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

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= gMaxParticles)
    {
        return;
    }

    GPUParticleState p = gParticles[idx];
    bool wasAlive = p.alive != 0u;
    bool alive = wasAlive && p.currentTime < p.lifeTime;

    // 寿命が尽きた瞬間に1回だけ、出し直すグループなら生まれ変わらせ、そうでなければ枠を空き一覧へ返す
    if (wasAlive && !alive)
    {
        if (gLoopEnabled != 0u)
        {
            p = RespawnLoopParticle(idx);
            alive = true;
        }
        else
        {
            p.alive = 0u;
            gParticles[idx] = p;
            PushFreeSlot(gCounter, gFreeList, idx);
        }
    }

    // 死んでいる枠は透明にしておく（頂点シェーダーが描かずに捨てる）
    if (!alive)
    {
        gInstancing[idx].color = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

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

    // curveFlag == 9: 魂 — 加速しながら昇り、横の勢いは抜けて、左右に揺れる
    if (p.curveFlag == 9)
    {
        p.velocity.y += kSoulRise * gDeltaTime;
        p.velocity.xz *= saturate(1.0f - kSoulDrag * gDeltaTime);
        p.velocity.x += sin(p.currentTime * kSoulSwayHz + phase) * kSoulSway * gDeltaTime;
    }

    // curveFlag == 10: 粘液 — 重く落ちる
    if (p.curveFlag == 10)
    {
        p.velocity.y -= kGooGravity * gDeltaTime;
    }

    // curveFlag == 11: 舞い落ちる破片 — 速さが抜けてゆっくり落ち、左右に揺れながら回る
    if (p.curveFlag == 11)
    {
        p.velocity.y -= kFlutterGravity * gDeltaTime;
        p.velocity *= saturate(1.0f - kFlutterDrag * gDeltaTime);
        p.velocity.x += sin(p.currentTime * kFlutterSwayHz + phase) * kFlutterSway * gDeltaTime;
        p.rotateZ += kFlutterSpin * spinSign * gDeltaTime;
    }

    // 場の力（衝撃で押し広げる・渦へ吸い込む・突進に引きずられる）
    if (gFieldScale > 0.0f)
    {
        p.velocity = ApplyFields(p.position, p.velocity);
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

    // 魂: 昇るにつれてふくらむ
    if (p.curveFlag == 9)
    {
        renderScale *= 1.0f + kSoulGrow * t;
    }
    // 粘液: 飛ぶ向きへ伸ばし、伸びた分だけ細くする
    if (p.curveFlag == 10)
    {
        float speed = length(p.velocity.xy);
        float stretch = 1.0f + kGooStretch * saturate(speed / kGooStretchSpeed);
        renderScale.y *= stretch;
        renderScale.x *= max(1.0f / stretch, kGooMinThin);
        // 板の縦（+Y）を飛ぶ向きへ合わせる
        renderRot = speed > 0.0001f ? atan2(p.velocity.y, p.velocity.x) - kPi * 0.5f : p.rotateZ;
    }
    // 舞い落ちる破片: 裏返るように幅を縮めたり戻したりする
    if (p.curveFlag == 11)
    {
        renderScale.x *= max(abs(cos(p.currentTime * kFlutterFlipHz + phase)), kFlutterMinWidth);
    }

    // 放電: 一定間隔ごとに位置と向きを跳ばす（積み上げずに描く時だけずらす）
    uint crackleSlot = (uint) (p.currentTime * kCrackleHz);
    uint4 crackleRng = XoshiroSeed(crackleSlot, idx * 2654435761u, idx);
    if (p.curveFlag == 8)
    {
        float jump = kCrackleJump * max(p.scale.x, p.scale.y);
        renderPos.x += (Rand01(crackleRng) * 2.0f - 1.0f) * jump;
        renderPos.y += (Rand01(crackleRng) * 2.0f - 1.0f) * jump;
        renderRot = Rand01(crackleRng) * kTwoPi;
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
        uint4 rng = XoshiroSeed(timeSlot, idx * 3141592653u, idx);
        float r = Rand01(rng);
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
        bool lit = Rand01(crackleRng) < kCrackleOnRate;
        float3 rgb = lerp(p.color.rgb, float3(1.0f, 1.0f, 1.0f), Rand01(crackleRng) * kCrackleWhite);
        gInstancing[idx].color = float4(rgb, lit ? p.color.a * (1.0f - t * t) : 0.0f);
    }
    else if (p.curveFlag == 9)
    {
        // 魂: 白く抜け出して指定色になり、ふわっと現れてふわっと消える
        float3 rgb = lerp(float3(1.0f, 1.0f, 1.0f), p.color.rgb, saturate(t * 2.0f + (1.0f - kSoulWhiten)));
        gInstancing[idx].color = float4(rgb, p.color.a * sin(t * kPi));
    }
    else if (p.curveFlag == 10)
    {
        // 粘液: しばらくはっきり見せてから薄れる
        gInstancing[idx].color = float4(p.color.rgb, p.color.a * (1.0f - saturate((t - kGooFadeStart) / (1.0f - kGooFadeStart))));
    }
    else
    {
        float alpha = 1.0f - (p.currentTime / p.lifeTime);
        gInstancing[idx].color = float4(p.color.rgb, p.color.a * alpha);
    }
}
