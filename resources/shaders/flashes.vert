struct VSInput {
    float2 v : POSITION;
};

struct VSOutput {
    float4 position : SV_Position;
};

cbuffer FlashesVertexConstants : register(b0) {
    float time;
    float2 inverseScreenSize;
    float userScale;
    float keyboardHeight;
    int minNote;
    float notesCount;
    int globalId;
    int horizontalMode;
};

static const float shifts[128] = { 0.0,0.5,1.0,1.5,2.0,3.0,3.5,4.0,4.5,5.0,5.5,6.0,7.0,7.5,8.0,8.5,9.0,10.0,10.5,11.0,11.5,12.0,12.5,13.0,14.0,14.5,15.0,15.5,16.0,17.0,17.5,18.0,18.5,19.0,19.5,20.0,21.0,21.5,22.0,22.5,23.0,24.0,24.5,25.0,25.5,26.0,26.5,27.0,28.0,28.5,29.0,29.5,30.0,31.0,31.5,32.0,32.5,33.0,33.5,34.0,35.0,35.5,36.0,36.5,37.0,38.0,38.5,39.0,39.5,40.0,40.5,41.0,42.0,42.5,43.0,43.5,44.0,45.0,45.5,46.0,46.5,47.0,47.5,48.0,49.0,49.5,50.0,50.5,51.0,52.0,52.5,53.0,53.5,54.0,54.5,55.0,56.0,56.5,57.0,58.0,58.5,59.0,59.5,60.0,60.5,61.0,61.5,62.0,63.0,63.5,64.0,64.5,65.0,66.0,66.5,67.0,67.5,68.0,68.5,69.0,70.0,70.5,71.0,71.5,72.0,73.0,73.5,74.0
};

static const float2 flashScale = 0.9 * float2(3.5,3.0);

float2 flipIfNeeded(float2 p) {
    return horizontalMode != 0 ? float2(p.y,-p.x) : p;
}

VSOutput main(VSInput input) {
    VSOutput output;

    float screenRatio = inverseScreenSize.y / inverseScreenSize.x;
    float2 scalingFactor = float2(1.0,horizontalMode != 0 ? 1.0 / screenRatio : screenRatio);

    float2 scaledPosition = input.v * 2.0 * flashScale * userScale / notesCount * scalingFactor;

    float x = -1.0 + ((shifts[globalId] - shifts[minNote]) * 2.0 + 1.0) / notesCount;
    float y = 2.0 * keyboardHeight - 1.0;

    float2 position = scaledPosition + float2(x,y);

    output.position = float4(flipIfNeeded(position),0.0,1.0);
    return output;
}
