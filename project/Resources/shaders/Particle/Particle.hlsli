struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : TEXCOORD3;
    float4 color : COLOR0;
    float2 maskcoord : TEXCOORD1; // 丸く切り抜くための、コマ送り前の UV
    float4 params : TEXCOORD2; // x=焼け具合 y=熱さ z=1なら丸く切り抜かない
    float viewDepth : TEXCOORD4; // カメラから見た奥行き（ソフトパーティクルで、後ろの物との近さを測る）
};
