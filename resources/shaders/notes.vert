struct VSInput {
 float2 v : POSITION;
 float note : NOTE_DATA0;
 float start : NOTE_DATA1;
 float duration : NOTE_DATA2;
 float isMinor : NOTE_DATA3;
 float set : NOTE_DATA4;
};

struct VSOutput {
 float4 position : SV_Position;
 float2 uv : TEXCOORD0;
 float2 noteSize : TEXCOORD1;
 nointerpolation float2 noteCorner : TEXCOORD2;
 float isMinor : TEXCOORD3;
 float channel : TEXCOORD4;
};

cbuffer NotesVertexConstants : register(b0) {
 float time;
 float mainSpeed;
 float minorsWidth;
 float keyboardHeight;
 int minNoteMajor;
 float notesCount;
 int reverseMode;
 int horizontalMode;
};

#define MAJOR_COUNT 75
static const int minorIds[MAJOR_COUNT] = { 1, 3, 0, 6, 8, 10, 0, 13, 15, 0, 18, 20, 22, 0, 25, 27, 0, 30, 32, 34, 0, 37, 39, 0, 42, 44, 46, 0, 49, 51, 0, 54, 56, 58, 0, 61, 63, 0, 66, 68, 70, 0, 73, 75, 0, 78, 80, 82, 0, 85, 87, 0, 90, 92, 94, 0, 97, 99, 0, 102, 104, 106, 0, 109, 111, 0, 114, 116, 118, 0, 121, 123, 0, 126, 0 };

float minorShift(int id) {
 if (id == 1 || id == 6) return -0.1;
 if (id == 3 || id == 10) return 0.1;
 return 0.0;
}

float2 flipIfNeeded(float2 inPos) {
 return horizontalMode != 0 ? float2(inPos.y, -inPos.x) : inPos;
}

VSOutput main(VSInput input) {
 VSOutput output;

 float scalingFactor = input.isMinor != 0.0 ? minorsWidth : 1.0;

 output.noteSize = float2(
 0.9 * 2.0 / notesCount * scalingFactor,
 input.duration * mainSpeed
 );

 float a = 2.0;
 float b = -notesCount + 1.0 - 2.0 * (float)minNoteMajor;
 float horizLoc = (input.note * a + b + input.isMinor) / notesCount;

 float vertLoc = 2.0 * keyboardHeight - 1.0;

 vertLoc +=
 (reverseMode != 0 ? -1.0 : 1.0) *
 (output.noteSize.y * 0.5 + mainSpeed * (input.start - time));

 float2 noteShift = float2(horizLoc, vertLoc);

 int noteIndex = (int)input.note;

 if (noteIndex >= 0 && noteIndex < MAJOR_COUNT) {
 noteShift.x +=
 input.isMinor *
 minorShift(minorIds[noteIndex] % 12) *
 output.noteSize.x;
 }

 output.uv = output.noteSize * input.v;
 output.isMinor = input.isMinor;
 output.channel = input.set;

 output.position = float4(
 flipIfNeeded(output.noteSize * input.v + noteShift),
 0.0,
 1.0
 );

 output.noteCorner = flipIfNeeded(
 output.noteSize *
 float2(
 -0.5,
 reverseMode != 0 ? -0.5 : 0.5
 ) + noteShift
 );

 return output;
}
