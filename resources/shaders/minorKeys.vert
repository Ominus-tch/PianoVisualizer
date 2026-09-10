struct VSInput {
    float2 v : POSITION;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float id : TEXCOORD1;
};

cbuffer MinorKeysVertexConstants : register(b0) {
    int horizontalMode;
    float keyboardHeight;
    float notesCount;
    int minNoteMajor;
    float minorsHeight;
    float minorsWidth;
    int4 actives[32];
};

static const int minorIds[53] = { 1,3,6,8,10,13,15,18,20,22,25,27,30,32,34,37,39,42,44,46,49,51,54,56,58,61,63,66,68,70,73,75,78,80,82,85,87,90,92,94,97,99,102,104,106,109,111,114,116,118,121,123,126 };
static const int noteDelta[12] = { 0,0,1,1,2,3,3,4,4,5,5,6 };

float minorShift(int id) {
    if (id == 1 || id == 6) return -0.1;
    if (id == 3 || id == 10) return 0.1;
    return 0.0;
}

float2 flipIfNeeded(float2 inPos) {
    return horizontalMode != 0 ? float2(inPos.y,-inPos.x) : inPos;
}

VSOutput main(VSInput input,uint instanceId : SV_InstanceID) {
    VSOutput output;
    float noteWidth = 2.0 / notesCount;
    float2 noteSize = float2(0.9 * noteWidth * minorsWidth,2.0 * minorsHeight * keyboardHeight);
    float a = 2.0;
    float b = -notesCount + 1.0 - 2.0 * (float)minNoteMajor;
    int minorId = minorIds[instanceId];
    int noteId = (minorId / 12) * 7 + noteDelta[minorId % 12];
    float horizLoc = ((float)noteId * a + b + 1.0) / notesCount;
    float vertLoc = 2.0 * (1.0 - minorsHeight) * keyboardHeight - 1.0 + noteSize.y * 0.5;
    float2 noteShift = float2(horizLoc,vertLoc);
    noteShift.x += minorShift(minorId % 12) * noteSize.x;
    float2 finalPosition = noteSize * input.v + noteShift;
    if (abs(noteShift.x) >= 1.0 - 0.5 * noteWidth) finalPosition = float2(-40000.0,-40000.0);
    output.position = float4(flipIfNeeded(finalPosition),0.0,1.0);
    output.uv = input.v + 0.5;
    output.id = (float)actives[minorId / 4][minorId % 4];
    return output;
}
