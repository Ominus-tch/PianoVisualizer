#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <glm/glm.hpp>

#include "../midi/MIDIFile.h"
#include "State.h"
#include "../helpers/ProgramUtilities.h"

#include <fstream>
#include <memory>

using Microsoft::WRL::ComPtr;

class MIDIScene;

class Renderer
{
public:

    void renderSetup(
        ID3D11Device* device,
        ID3D11DeviceContext* context
    );

    void upload(
        const std::shared_ptr<MIDIScene>& scene
    );

    void setScaleAndMinorWidth(
        float scale,
        float minorWidth
    );

    void setParticlesParameters(
        float speed,
        float expansion
    );

    void setKeyboardSizeAndFadeout(
        float keyboardHeight,
        float fadeOut
    );

    void setMinorEdgesAndHeight(
        bool minorEdges,
        float minorHeight
    );

    void setMinMaxKeys(
        int minKey,
        int minKeyMajor,
        int notesCount
    );

    void setOrientation(bool horizontal);

    void drawNotes(
        const std::shared_ptr<MIDIScene>& scene,
        float time,
        const glm::vec2& invScreenSize,
        const State::NotesState& state,
        bool reverseScroll,
        bool prepass
    );

    void clearFlashes()
    {
        _flashes.fill(
            {
                -1,
                0.0f,
                0.0f,
                false
            }
        );
    }

    void drawFlashes(
        const std::shared_ptr<MIDIScene>& scene,
        float time,
        const glm::vec2& invScreenSize,
        const State::FlashesState& state
    );

    void drawParticles(
        const std::shared_ptr<MIDIScene>& scene,
        float time,
        const glm::vec2& invScreenSize,
        const State::ParticlesState& state,
        bool prepass
    );

    void drawKeyboard(
        const std::shared_ptr<MIDIScene>& scene,
        float time,
        const glm::vec2& invScreenSize,
        const glm::vec3& edgeColor,
        const glm::vec3& keyColor,
        const ColorArray& majorColors,
        const ColorArray& minorColors,
        bool highlightKeys
    );

    void drawPedals(
        const std::shared_ptr<MIDIScene>& scene,
        float time,
        const glm::vec2& invScreenSize,
        const State::PedalsState& state,
        float keyboardHeight,
        bool horizontalMode
    );

    void drawWaves(
        const std::shared_ptr<MIDIScene>& scene,
        float time,
        const glm::vec2& invScreenSize,
        const State::WaveState& state,
        float keyboardHeight
    );

    void drawScore(
        const std::shared_ptr<MIDIScene>& scene,
        float time,
        const glm::vec2& invScreenSize,
        const State::ScoreState& state,
        float measureScale,
        float qualityScale,
        float keyboardHeight,
        bool horizontalMode,
        bool reverseScroll
    );

    void clean();

private:

    ShaderProgram _programNotes;
    ShaderProgram _programFlashes;
    ShaderProgram _programParticules;
    ShaderProgram _programKeyMinors;
    ShaderProgram _programKeyMajors;
    ShaderProgram _programPedals;
    ShaderProgram _programWave;
    ShaderProgram _programWaveNoise;
    ShaderProgram _programScoreBars;
    ShaderProgram _programScoreLabels;

    ID3D11Device* _device = nullptr;
    ID3D11DeviceContext* _context = nullptr;

    ComPtr<ID3D11BlendState> _additiveBlendState;

    // --------------------------------------------------------
    // Basic quad
    // --------------------------------------------------------

    ComPtr<ID3D11Buffer> _quadVertices;
    ComPtr<ID3D11Buffer> _quadIndices;

    // --------------------------------------------------------
    // Dynamic note/key data
    // --------------------------------------------------------

    ComPtr<ID3D11Buffer> _notesDataBuffer;
    //ComPtr<ID3D11Buffer> _keysDataBuffer;

    // --------------------------------------------------------
    // Wave geometry
    // --------------------------------------------------------

    ComPtr<ID3D11Buffer> _waveVertices;
    ComPtr<ID3D11Buffer> _waveIndices;

    // --------------------------------------------------------
    // Counts
    // --------------------------------------------------------

    UINT _quadIndexCount = 0;
    UINT _waveIndexCount = 0;

    // --------------------------------------------------------
    // Textures
    // --------------------------------------------------------

    ComPtr<ID3D11ShaderResourceView> _texParticles;
    ComPtr<ID3D11ShaderResourceView> _texFont;
    ComPtr<ID3D11ShaderResourceView> _texNoise;

    // Cached info.
    unsigned int _minKeyMajor{ 0 };
    unsigned int _keyCount{ 128 };

    struct FlashState
    {
        int colorId = -1;
        float fadeStart = 0.0f;
        float fadeFrom = 0.0f;
        bool active = false;
    };

    std::array<FlashState, 128> _flashes{};
};