#pragma once

#include "CameraControls.h"
#include "CameraVideoEncoder.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfobjects.h>

#include <wincodec.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>


class Camera
{
public:

    // =========================================================
    // Camera format
    // =========================================================

    enum class CameraFormat
    {
        None,
        NV12,
        MJPG
    };

    struct CameraMode
    {
        int width = 0;
        int height = 0;

        UINT32 fpsNumerator = 0;
        UINT32 fpsDenominator = 1;

        double GetFPS() const
        {
            if (fpsDenominator == 0)
            {
                return 0.0;
            }

            return
                static_cast<double>(fpsNumerator) /
                static_cast<double>(fpsDenominator);
        }
    };


    // =========================================================
    // Statistics
    // =========================================================

    struct CameraStatistics
    {
        int width = 0;
        int height = 0;

        UINT32 fpsNumerator = 0;
        UINT32 fpsDenominator = 1;

        CameraFormat format =
            CameraFormat::None;

        double captureFps = 0.0;
        double captureFrameTimeMs = 0.0;

        double uploadFps = 0.0;
        double uploadFrameTimeMs = 0.0;

        uint64_t framesCaptured = 0;
        uint64_t framesUploaded = 0;
        uint64_t framesDropped = 0;

        size_t frameBytes = 0;

        bool open = false;
        bool threadRunning = false;
    };


public:

    // =========================================================
    // Construction
    // =========================================================

    Camera();
    ~Camera();


    // =========================================================
    // Initialization
    // =========================================================

    bool Initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context
    );

    void Shutdown();

    std::string GetLastError() const;
    void ClearLastError();


    // =========================================================
    // Runtime update
    // =========================================================

    bool Update();

    // =========================================================
    // Recording
    // =========================================================

    bool StartRecording(
        const std::string& filePath
    );

    bool StopRecording();

    bool IsRecording() const;

    // =========================================================
    // Camera selection
    // =========================================================

    struct CameraDevice
    {
        std::string name;
        int index = -1;
    };

    bool EnumerateCameras();

    const std::vector<CameraDevice>& GetAvailableCameras() const;
    int GetCameraIndex() const;

    bool OpenCamera(
        int cameraIndex
    );

    void CloseCamera();

    // =========================================================
    // Camera format
    // =========================================================

    bool SetFormat(
        CameraFormat format
    );

    CameraFormat GetFormat() const;

    // =========================================================
    // Camera Settings
    // =========================================================

    const std::vector<CameraMode>&
        GetAvailableModes() const;

    bool SetMode(
        int width,
        int height,
        UINT32 fpsNumerator,
        UINT32 fpsDenominator
    );

    CameraControls& Controls();
    const CameraControls& Controls() const;

    // =========================================================
    // Resolution
    // =========================================================

    bool Resize(
        int width,
        int height
    );


    // =========================================================
    // State
    // =========================================================

    bool IsOpen() const;


    // =========================================================
    // Rendering
    // =========================================================

    ID3D11ShaderResourceView*
        GetTexture() const;


    // =========================================================
    // Dimensions
    // =========================================================

    int GetWidth() const;
    int GetHeight() const;


    // =========================================================
    // Statistics
    // =========================================================

    CameraStatistics GetStatistics() const;


private:

    // =========================================================
    // Camera thread
    // =========================================================

    void CameraThread();

    bool CaptureFrame();

    // =========================================================
    // Media Foundation
    // =========================================================

    bool FindCamera(
        int cameraIndex,
        IMFActivate** camera
    );

    bool CreateReader(
        IMFActivate* camera,
        CameraFormat requestedFormat
    );

    // =========================================================
    // D3D11 resources
    // =========================================================

    bool CreateTexture();

    bool CreateBGRATexture();

    bool CreateNV12Textures();

    bool CreateNV12ConversionResources();

    bool CreateNV12ConversionShaders();

    void ReleaseNV12Resources();


    // =========================================================
    // Frame upload
    // =========================================================

    bool UploadFrame(
        const BYTE* pixels,
        size_t pixelSize
    );

    bool UploadNV12Frame(
        const BYTE* data,
        size_t dataSize
    );

    bool UploadMJPGFrame(
        const BYTE* pixels,
        size_t pixelSize
    );

    // =========================================================
    // NV12 conversion
    // =========================================================

    bool ConvertNV12ToBGRA();

    // =========================================================
    // Format helpers
    // =========================================================

    static bool IsNV12(
        CameraFormat format
    );

    static bool IsMJPG(
        CameraFormat format
    );

    size_t GetExpectedNV12FrameSize() const;

    size_t GetExpectedBGRASize() const;


private:

    // =========================================================
    // D3D11
    // =========================================================

    ID3D11Device*
        m_device = nullptr;

    ID3D11DeviceContext*
        m_context = nullptr;

    mutable std::mutex m_errorMutex;
    std::string m_lastError;


    // =========================================================
    // Final BGRA texture
    //
    // This is the texture returned by GetTexture().
    //
    // NV12:
    //
    //     NV12 Y/UV textures
    //             |
    //             v
    //       NV12 conversion shader
    //             |
    //             v
    //       BGRA render target
    //             |
    //             v
    //       m_shaderResourceView
    //
    // MJPG:
    //
    //       JPEG
    //         |
    //       WIC
    //         |
    //       BGRA
    //         |
    //         v
    //       BGRA texture
    // =========================================================

    Microsoft::WRL::ComPtr<ID3D11Texture2D>
        m_texture;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
        m_shaderResourceView;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>
        m_renderTargetView;


    // =========================================================
    // NV12 textures
    //
    // Y:
    //
    //     R8_UNORM
    //     width  = camera width
    //     height = camera height
    //
    // UV:
    //
    //     R8G8_UNORM
    //     width  = camera width / 2
    //     height = camera height / 2
    // =========================================================

    Microsoft::WRL::ComPtr<ID3D11Texture2D>
        m_nv12YTexture;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
        m_nv12YSRV;


    Microsoft::WRL::ComPtr<ID3D11Texture2D>
        m_nv12UVTexture;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
        m_nv12UVSRV;


    // =========================================================
    // NV12 conversion shader
    // =========================================================

    Microsoft::WRL::ComPtr<ID3D11VertexShader>
        m_nv12VertexShader;

    Microsoft::WRL::ComPtr<ID3D11PixelShader>
        m_nv12PixelShader;

    Microsoft::WRL::ComPtr<ID3D11SamplerState>
        m_nv12Sampler;


    // =========================================================
    // Shader bytecode
    //
    // Kept so the vertex shader bytecode remains available if
    // an input layout or further shader work is required later.
    // =========================================================

    Microsoft::WRL::ComPtr<ID3DBlob>
        m_nv12VertexShaderBlob;

    Microsoft::WRL::ComPtr<ID3DBlob>
        m_nv12PixelShaderBlob;


    // =========================================================
    // Media Foundation
    // =========================================================

    Microsoft::WRL::ComPtr<IMFSourceReader>
        m_reader;


    // =========================================================
    // WIC
    //
    // Used only for MJPG decoding.
    // =========================================================

    Microsoft::WRL::ComPtr<IWICImagingFactory>
        m_wicFactory;


    // =========================================================
    // Media Foundation state
    // =========================================================

    bool m_mediaFoundationStarted =
        false;


    // =========================================================
    // Camera selection
    // =========================================================

    std::vector<CameraDevice>
        m_availableCameras;

    int m_cameraIndex =
        0;


    // =========================================================
    // Camera state
    // =========================================================

    int m_width =
        0;

    int m_height =
        0;


    UINT32 m_fpsNumerator =
        0;

    UINT32 m_fpsDenominator =
        1;


    CameraFormat m_format =
        CameraFormat::None;


    CameraFormat m_requestedFormat =
        CameraFormat::NV12;


    // =========================================================
    // Frame buffer sizes
    // =========================================================

    size_t m_frameBufferSize =
        0;


    // NV12:

    size_t m_nv12YSize =
        0;

    size_t m_nv12UVSize =
        0;


    // =========================================================
    // CPU frame buffers
    //
    // Capture thread writes to m_captureBuffer.
    //
    // Render thread consumes m_uploadBuffer.
    //
    // m_readyBuffer contains the latest completed frame.
    // =========================================================

    std::vector<BYTE>
        m_captureBuffer;

    std::vector<BYTE>
        m_readyBuffer;

    std::vector<BYTE>
        m_uploadBuffer;


    // =========================================================
    // Frame synchronization
    // =========================================================

    std::mutex
        m_frameMutex;

    std::atomic<bool>
        m_newFrameAvailable =
        false;

    // =========================================================
    // Camera Settings
    // =========================================================

    std::vector<CameraMode>
        m_availableModes;

    CameraControls m_controls;

    bool m_autoExposure = true;

    bool m_lowLightCompensation = false;

    CameraMode m_requestedMode;


    // =========================================================
    // Camera thread
    // =========================================================

    std::thread
        m_cameraThread;

    std::atomic<bool> 
        m_runtimeFailure = 
        false;

    std::atomic<bool>
        m_running =
        false;

    std::atomic<bool>
        m_open =
        false;

    // =========================================================
    // Recording state
    // =========================================================

    CameraVideoEncoder m_videoEncoder;

    struct RecordedFrame
    {
        std::vector<uint8_t> pixels;
        int width = 0;
        int height = 0;
        double timestamp = 0.0;
    };

    std::atomic<bool>
        m_recording =
        false;

    std::mutex
        m_recordingMutex;

    std::string
        m_recordingFilePath;

    std::chrono::steady_clock::time_point
        m_recordingStartTime{};

    std::vector<RecordedFrame> m_recordedFrames;


    // =========================================================
    // Capture statistics
    // =========================================================

    std::atomic<uint64_t>
        m_framesCaptured =
        0;

    std::atomic<uint64_t>
        m_framesDropped =
        0;

    std::atomic<size_t>
        m_captureFrameBytes =
        0;


    std::atomic<double>
        m_captureFps =
        0.0;

    std::atomic<double>
        m_captureFrameTimeMs =
        0.0;

    // =========================================================
    // Upload statistics
    // =========================================================

    std::atomic<uint64_t>
        m_framesUploaded =
        0;


    std::atomic<double>
        m_uploadFps =
        0.0;

    std::atomic<double>
        m_uploadFrameTimeMs =
        0.0;


    // =========================================================
    // Upload FPS calculation
    // =========================================================

    std::chrono::steady_clock::time_point
        m_uploadStatsTime{};

    uint64_t
        m_uploadStatsFrameCount =
        0;
};