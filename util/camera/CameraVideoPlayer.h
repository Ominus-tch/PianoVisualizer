#pragma once

#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;


struct CameraVideoPlayerStatistics
{
    int width = 0;
    int height = 0;

    UINT32 fpsNumerator = 0;
    UINT32 fpsDenominator = 1;

    double videoFps = 0.0;

    double decodeFps = 0.0;
    double decodeFrameTimeMs = 0.0;

    double uploadFps = 0.0;
    double uploadFrameTimeMs = 0.0;

    uint64_t framesDecoded = 0;
    uint64_t framesUploaded = 0;
    uint64_t framesDropped = 0;

    size_t frameBytes = 0;

    size_t queuedFrames = 0;
    double bufferedTimeMs = 0.0;

    double currentFrameTime = 0.0;
    double targetTime = 0.0;

    double decoderLeadMs = 0.0;

    bool open = false;
    bool threadRunning = false;
    bool endOfStream = false;
};


class CameraVideoPlayer
{
public:

    CameraVideoPlayer();
    ~CameraVideoPlayer();


    bool Initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context
    );

    void Shutdown();


    bool Open(
        const std::string& filePath
    );

    void Close();


    bool Update(
        double time
    );


    ID3D11ShaderResourceView*
        GetTexture() const
    {
        return m_shaderResourceView.Get();
    }


    int GetWidth() const
    {
        return m_width.load(
            std::memory_order_acquire
        );
    }


    int GetHeight() const
    {
        return m_height.load(
            std::memory_order_acquire
        );
    }


    bool IsOpen() const
    {
        return m_open.load(
            std::memory_order_acquire
        );
    }


    const std::string& GetFilePath() const
    {
        return m_filePath;
    }


    CameraVideoPlayerStatistics
        GetStatistics() const;


private:

    struct DecodedFrame
    {
        double time = 0.0;

        std::vector<uint8_t>
            pixels;
    };


private:

    void DecodeThread(
        std::string filePath
    );


    bool DecodeSample(
        IMFSample* sample,
        LONG sourceStride,
        int width,
        int height,
        std::vector<uint8_t>& output
    );


    bool CreateTexture();


    bool RequestSeek(
        double time
    );


private:

    static constexpr size_t
        MAX_BUFFERED_FRAMES = 6;


private:

    ID3D11Device*
        m_device = nullptr;

    ID3D11DeviceContext*
        m_context = nullptr;


    ComPtr<ID3D11Texture2D>
        m_texture;

    ComPtr<ID3D11ShaderResourceView>
        m_shaderResourceView;


    std::vector<uint8_t>
        m_uploadBuffer;


    std::atomic<int>
        m_width = 0;

    std::atomic<int>
        m_height = 0;


    std::atomic<UINT32>
        m_fpsNumerator = 0;

    std::atomic<UINT32>
        m_fpsDenominator = 1;


    std::atomic<uint64_t>
        m_framesDecoded = 0;

    std::atomic<uint64_t>
        m_framesUploaded = 0;

    std::atomic<uint64_t>
        m_framesDropped = 0;


    std::atomic<bool>
        m_threadRunning = false;

    std::atomic<bool>
        m_stopRequested = false;

    std::atomic<bool>
        m_open = false;

    std::atomic<bool>
        m_decoderFailed = false;


    std::atomic<double>
        m_lastDecodedFrameTime = 0.0;

    std::atomic<double>
        m_targetTime = 0.0;


    std::atomic<double>
        m_decodeFps = 0.0;

    std::atomic<double>
        m_decodeFrameTimeMs = 0.0;

    std::atomic<double>
        m_uploadFps = 0.0;

    std::atomic<double>
        m_uploadFrameTimeMs = 0.0;


    std::atomic<uint64_t>
        m_decodeStatFrameCount = 0;

    std::atomic<double>
        m_decodeFrameTimeAccumulatorMs = 0.0;

    std::atomic<uint64_t>
        m_uploadStatFrameCount = 0;

    std::atomic<double>
        m_uploadFrameTimeAccumulatorMs = 0.0;


    std::chrono::steady_clock::time_point
        m_decodeStatStart;

    std::chrono::steady_clock::time_point
        m_uploadStatStart;


    double
        m_currentFrameTime = 0.0;


    std::deque<DecodedFrame>
        m_frameQueue;

    mutable std::mutex
        m_queueMutex;

    std::condition_variable
        m_queueCondition;


    std::thread
        m_decodeThread;


    bool
        m_seekRequested = false;

    double
        m_seekTime = 0.0;

    bool
        m_endOfStream = false;


    std::string
        m_filePath;
};