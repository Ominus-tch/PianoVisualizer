struct VSInput {
    float2 v : POSITION;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float id : TEXCOORD1;
};

cbuffer ParticlesVertexConstants : register(b0) {
    float time;
    float scale;
    float2 inverseScreenSize;
    float2 inverseTextureSize;
    int globalId;
    float duration;
    int channel;
    int texCount;
    float colorScale;
    float expansionFactor;
    float speedScaling;
    float keyboardHeight;
    float turbulenceStrength;
    float turbulenceScale;
    int minNote;
    float notesCount;
    int horizontalMode;
};

Texture2D textureParticles : register(t0);
Texture2D textureNoise : register(t1);
SamplerState particleSampler : register(s0);
SamplerState noiseSampler : register(s1);

static const float shifts[128] = { 0.0,0.5,1.0,1.5,2.0,3.0,3.5,4.0,4.5,5.0,5.5,6.0,7.0,7.5,8.0,8.5,9.0,10.0,10.5,11.0,11.5,12.0,12.5,13.0,14.0,14.5,15.0,15.5,16.0,17.0,17.5,18.0,18.5,19.0,19.5,20.0,21.0,21.5,22.0,22.5,23.0,24.0,24.5,25.0,25.5,26.0,26.5,27.0,28.0,28.5,29.0,29.5,30.0,31.0,31.5,32.0,32.5,33.0,33.5,34.0,35.0,35.5,36.0,36.5,37.0,38.0,38.5,39.0,39.5,40.0,40.5,41.0,42.0,42.5,43.0,43.5,44.0,45.0,45.5,46.0,46.5,47.0,47.5,48.0,49.0,49.5,50.0,50.5,51.0,52.0,52.5,53.0,53.5,54.0,54.5,55.0,56.0,56.5,57.0,57.5,58.0,59.0,59.5,60.0,60.5,61.0,61.5,62.0,63.0,63.5,64.0,64.5,65.0,66.0,66.5,67.0,67.5,68.0,68.5,69.0,70.0,70.5,71.0,71.5,72.0,73.0,73.5,74.0
};



float rand(float2 co) {
    return frac(sin(dot(co,float2(12.9898,78.233))) * 43758.5453);
}

float2 flipIfNeeded(float2 inPos) {
    return horizontalMode != 0 ? float2(inPos.y,-inPos.x) : inPos;
}

VSOutput main(VSInput input,uint instanceId : SV_InstanceID) {
    VSOutput output;

    output.id = (float)(instanceId % (uint)texCount);
    output.uv = input.v + 0.5;

    float localTime = speedScaling * time * duration;
    float particlesCount = 1.0 / inverseTextureSize.y;

    float particleId = (float)instanceId + floor(particlesCount * 10.0 * rand(float2((float)globalId,(float)globalId)));
    float textureId = fmod(particleId,particlesCount);
    float particleShift = floor(particleId / particlesCount);

    float2 particleUV = float2(localTime / inverseTextureSize.x + 10.0 * particleShift,textureId);
    particleUV = (particleUV + 0.5) * inverseTextureSize;
    particleUV.x = clamp(particleUV.x,0.0,1.0);

    float3 position = textureParticles.SampleLevel(particleSampler,particleUV,0.0).xyz;
    position.x -= 0.5;

    float2 shift = 0.5 * position.xy;
    float random = rand(float2(particleId + (float)globalId,time * 0.000002 + 100.0 * (float)globalId));
    shift += float2(0.0,0.1 * random);

    shift = shift * time * expansionFactor;
    shift.x *= max(0.5,pow(shift.y,0.3));

    float xshift = -1.0 + ((shifts[globalId] - shifts[minNote]) * 2.0 + 1.0) / notesCount;
    float2 globalShift = float2(xshift,(2.0 * keyboardHeight - 1.0) - 0.02);

    float2 localShift = shift * duration * float2(1.0,0.5);
    float2 vertexShift = 0.003 * scale * input.v;

    float screenRatio = inverseScreenSize.y / inverseScreenSize.x;
    float2 screenScaling = float2(1.0,horizontalMode != 0 ? 1.0 / screenRatio : screenRatio);

    float2 particlePos = globalShift + screenScaling * localShift;

    float2 curlNoise = textureNoise.SampleLevel(noiseSampler,turbulenceScale * particlePos.xy / screenScaling,0.0).gb;
    curlNoise.x = 2.0 * curlNoise.x - 1.0;

    float2 curlShift = 0.01 * turbulenceStrength * time * curlNoise;
    float2 finalPos = particlePos + screenScaling * (vertexShift + curlShift);

    finalPos = lerp(float2(-200.0,-200.0),finalPos,position.z);
    output.position = float4(flipIfNeeded(finalPos),0.0,1.0);

    return output;
}
