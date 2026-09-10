struct PSInput {
    float2 uv : TEXCOORD0;
    float id : TEXCOORD1;
};

Texture2DArray lookParticles : register(t0);
SamplerState particleSampler : register(s0);
cbuffer ParticlesPixelConstants : register(b0) {
    float time;
    float colorScale;
    int channel;    
float4 baseColor[12];
}

float4 main(PSInput input) : SV_Target {
    float alpha = lookParticles.Sample(particleSampler,float3(input.uv,input.id)).r;
    float4 color = float4(colorScale * baseColor[channel].rgb,1.0 - time * time);
    color.a *= alpha;
    return color;
}
