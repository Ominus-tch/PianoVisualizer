#include "CameraVideoPlayer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

#include "../../../util/Logger.h"


namespace
{

    constexpr LONGLONG
        HNS_PER_SECOND = 10'000'000LL;

    constexpr double
        STAT_INTERVAL_SECONDS = 0.5;

}


CameraVideoPlayer::CameraVideoPlayer()
{
}


CameraVideoPlayer::~CameraVideoPlayer()
{
    Shutdown();
}


bool CameraVideoPlayer::Initialize(
    ID3D11Device* device,
    ID3D11DeviceContext* context
)
{
    if (!device || !context)
    {
        Logger::Log(
            "[CameraVideoPlayer] Invalid D3D11 device/context.\n"
        );

        return false;
    }

    m_device =
        device;

    m_context =
        context;

    return true;
}


void CameraVideoPlayer::Shutdown()
{
    Close();

    m_device =
        nullptr;

    m_context =
        nullptr;
}


bool CameraVideoPlayer::Open(
    const std::string& filePath
)
{
    Close();

    if (
        !m_device ||
        !m_context ||
        filePath.empty()
        )
    {
        return false;
    }


    const std::filesystem::path path =
        filePath;


    if (!std::filesystem::exists(path))
    {
        Logger::Log(
            "[CameraVideoPlayer] File does not exist: %s\n",
            filePath.c_str()
        );

        return false;
    }


    m_filePath =
        filePath;


    m_currentFrameTime =
        0.0;


    m_width.store(
        0,
        std::memory_order_release
    );

    m_height.store(
        0,
        std::memory_order_release
    );


    m_fpsNumerator.store(
        0,
        std::memory_order_release
    );

    m_fpsDenominator.store(
        1,
        std::memory_order_release
    );


    m_framesDecoded.store(
        0,
        std::memory_order_release
    );

    m_framesUploaded.store(
        0,
        std::memory_order_release
    );

    m_framesDropped.store(
        0,
        std::memory_order_release
    );


    m_threadRunning.store(
        false,
        std::memory_order_release
    );

    m_stopRequested.store(
        false,
        std::memory_order_release
    );

    m_decoderFailed.store(
        false,
        std::memory_order_release
    );


    m_lastDecodedFrameTime.store(
        0.0,
        std::memory_order_release
    );

    m_targetTime.store(
        0.0,
        std::memory_order_release
    );


    m_decodeFps.store(
        0.0,
        std::memory_order_release
    );

    m_decodeFrameTimeMs.store(
        0.0,
        std::memory_order_release
    );

    m_uploadFps.store(
        0.0,
        std::memory_order_release
    );

    m_uploadFrameTimeMs.store(
        0.0,
        std::memory_order_release
    );


    m_decodeStatFrameCount.store(
        0,
        std::memory_order_release
    );

    m_decodeFrameTimeAccumulatorMs.store(
        0.0,
        std::memory_order_release
    );

    m_uploadStatFrameCount.store(
        0,
        std::memory_order_release
    );

    m_uploadFrameTimeAccumulatorMs.store(
        0.0,
        std::memory_order_release
    );


    const auto now =
        std::chrono::steady_clock::now();

    m_decodeStatStart =
        now;

    m_uploadStatStart =
        now;


    {
        std::lock_guard<std::mutex>
            lock(m_queueMutex);

        m_frameQueue.clear();

        m_seekRequested =
            false;

        m_seekTime =
            0.0;

        m_endOfStream =
            false;
    }


    m_uploadBuffer.clear();


    /*
     * Open is intentionally asynchronous.
     *
     * Media Foundation setup and decoding happen entirely on
     * the decoder thread so selecting a recording does not
     * block the render thread.
     */
    m_open.store(
        true,
        std::memory_order_release
    );


    m_decodeThread =
        std::thread(
            &CameraVideoPlayer::DecodeThread,
            this,
            filePath
        );


    Logger::Log(
        "[CameraVideoPlayer] Starting decoder: %s\n",
        filePath.c_str()
    );


    return true;
}


void CameraVideoPlayer::Close()
{
    m_open.store(
        false,
        std::memory_order_release
    );

    m_stopRequested.store(
        true,
        std::memory_order_release
    );


    m_queueCondition.notify_all();


    if (m_decodeThread.joinable())
    {
        m_decodeThread.join();
    }


    {
        std::lock_guard<std::mutex>
            lock(m_queueMutex);

        m_frameQueue.clear();

        m_seekRequested =
            false;

        m_seekTime =
            0.0;

        m_endOfStream =
            false;
    }


    m_uploadBuffer.clear();


    m_shaderResourceView.Reset();
    m_texture.Reset();


    m_width.store(
        0,
        std::memory_order_release
    );

    m_height.store(
        0,
        std::memory_order_release
    );


    m_fpsNumerator.store(
        0,
        std::memory_order_release
    );

    m_fpsDenominator.store(
        1,
        std::memory_order_release
    );


    m_currentFrameTime =
        0.0;


    m_targetTime.store(
        0.0,
        std::memory_order_release
    );


    m_lastDecodedFrameTime.store(
        0.0,
        std::memory_order_release
    );


    m_decoderFailed.store(
        false,
        std::memory_order_release
    );


    m_threadRunning.store(
        false,
        std::memory_order_release
    );


    m_decodeFps.store(
        0.0,
        std::memory_order_release
    );

    m_decodeFrameTimeMs.store(
        0.0,
        std::memory_order_release
    );

    m_uploadFps.store(
        0.0,
        std::memory_order_release
    );

    m_uploadFrameTimeMs.store(
        0.0,
        std::memory_order_release
    );


    m_filePath.clear();
}


void CameraVideoPlayer::DecodeThread(
    std::string filePath
)
{
    m_threadRunning.store(
        true,
        std::memory_order_release
    );


    HRESULT comResult =
        CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED
        );


    const bool
        comInitialized =
        SUCCEEDED(comResult);


    if (
        FAILED(comResult) &&
        comResult != RPC_E_CHANGED_MODE
        )
    {
        Logger::Log(
            "[CameraVideoPlayer] CoInitializeEx failed: 0x%08X\n",
            static_cast<unsigned>(comResult)
        );

        m_decoderFailed.store(
            true,
            std::memory_order_release
        );

        m_threadRunning.store(
            false,
            std::memory_order_release
        );

        return;
    }


    const std::filesystem::path path =
        filePath;

    const std::wstring widePath =
        path.wstring();


    /*
     * ---------------------------------------------------------
     * Source reader attributes
     * ---------------------------------------------------------
     */

    ComPtr<IMFAttributes>
        attributes;


    HRESULT hr =
        MFCreateAttributes(
            &attributes,
            2
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoPlayer] MFCreateAttributes failed: 0x%08X\n",
            static_cast<unsigned>(hr)
        );

        m_decoderFailed.store(
            true,
            std::memory_order_release
        );

        if (comInitialized)
            CoUninitialize();

        m_threadRunning.store(
            false,
            std::memory_order_release
        );

        return;
    }


    /*
     * RGB32 is explicitly requested below. We leave Media
     * Foundation video processing enabled because disabling it
     * caused MF_E_INVALIDMEDIATYPE with this recording.
     *
     * The important part is that this work now happens on the
     * decoder thread.
     */
    hr =
        attributes->SetUINT32(
            MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,
            TRUE
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoPlayer] Failed to enable video processing: 0x%08X\n",
            static_cast<unsigned>(hr)
        );

        m_decoderFailed.store(
            true,
            std::memory_order_release
        );

        if (comInitialized)
            CoUninitialize();

        m_threadRunning.store(
            false,
            std::memory_order_release
        );

        return;
    }


    /*
     * ---------------------------------------------------------
     * Open MP4
     * ---------------------------------------------------------
     */

    ComPtr<IMFSourceReader>
        reader;


    hr =
        MFCreateSourceReaderFromURL(
            widePath.c_str(),
            attributes.Get(),
            &reader
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoPlayer] Failed to open video: 0x%08X\n",
            static_cast<unsigned>(hr)
        );

        m_decoderFailed.store(
            true,
            std::memory_order_release
        );

        if (comInitialized)
            CoUninitialize();

        m_threadRunning.store(
            false,
            std::memory_order_release
        );

        return;
    }


    /*
     * ---------------------------------------------------------
     * Request RGB32 output
     * ---------------------------------------------------------
     */

    ComPtr<IMFMediaType>
        outputType;


    hr =
        MFCreateMediaType(
            &outputType
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoPlayer] MFCreateMediaType failed: 0x%08X\n",
            static_cast<unsigned>(hr)
        );

        m_decoderFailed.store(
            true,
            std::memory_order_release
        );

        if (comInitialized)
            CoUninitialize();

        m_threadRunning.store(
            false,
            std::memory_order_release
        );

        return;
    }


    hr =
        outputType->SetGUID(
            MF_MT_MAJOR_TYPE,
            MFMediaType_Video
        );


    if (FAILED(hr))
    {
        m_decoderFailed.store(
            true,
            std::memory_order_release
        );

        if (comInitialized)
            CoUninitialize();

        m_threadRunning.store(
            false,
            std::memory_order_release
        );

        return;
    }


    hr =
        outputType->SetGUID(
            MF_MT_SUBTYPE,
            MFVideoFormat_RGB32
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoPlayer] Failed to set RGB32 subtype: 0x%08X\n",
            static_cast<unsigned>(hr)
        );

        m_decoderFailed.store(
            true,
            std::memory_order_release
        );

        if (comInitialized)
            CoUninitialize();

        m_threadRunning.store(
            false,
            std::memory_order_release
        );

        return;
    }


    hr =
        reader->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            nullptr,
            outputType.Get()
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoPlayer] Failed to set RGB32 output format: 0x%08X\n",
            static_cast<unsigned>(hr)
        );

        m_decoderFailed.store(
            true,
            std::memory_order_release
        );

        if (comInitialized)
            CoUninitialize();

        m_threadRunning.store(
            false,
            std::memory_order_release
        );

        return;
    }


    /*
     * ---------------------------------------------------------
     * Read actual output media type
     * ---------------------------------------------------------
     */

    ComPtr<IMFMediaType>
        currentType;


    hr =
        reader->GetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            &currentType
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoPlayer] Failed to get output media type: 0x%08X\n",
            static_cast<unsigned>(hr)
        );

        m_decoderFailed.store(
            true,
            std::memory_order_release
        );

        if (comInitialized)
            CoUninitialize();

        m_threadRunning.store(
            false,
            std::memory_order_release
        );

        return;
    }


    UINT32 width = 0;
    UINT32 height = 0;


    hr =
        MFGetAttributeSize(
            currentType.Get(),
            MF_MT_FRAME_SIZE,
            &width,
            &height
        );


    if (
        FAILED(hr) ||
        width == 0 ||
        height == 0
        )
    {
        Logger::Log(
            "[CameraVideoPlayer] Failed to get video dimensions.\n"
        );

        m_decoderFailed.store(
            true,
            std::memory_order_release
        );

        if (comInitialized)
            CoUninitialize();

        m_threadRunning.store(
            false,
            std::memory_order_release
        );

        return;
    }


    LONG sourceStride =
        static_cast<LONG>(
            width * 4
            );


    UINT32 strideUnsigned = 0;


    if (
        SUCCEEDED(
            currentType->GetUINT32(
                MF_MT_DEFAULT_STRIDE,
                &strideUnsigned
            )
        )
        )
    {
        sourceStride =
            static_cast<LONG>(
                strideUnsigned
                );
    }


    UINT32 fpsNumerator = 0;
    UINT32 fpsDenominator = 1;


    if (
        FAILED(
            MFGetAttributeRatio(
                currentType.Get(),
                MF_MT_FRAME_RATE,
                &fpsNumerator,
                &fpsDenominator
            )
        )
        )
    {
        fpsNumerator =
            0;

        fpsDenominator =
            1;
    }


    m_width.store(
        static_cast<int>(width),
        std::memory_order_release
    );

    m_height.store(
        static_cast<int>(height),
        std::memory_order_release
    );


    m_fpsNumerator.store(
        fpsNumerator,
        std::memory_order_release
    );

    m_fpsDenominator.store(
        fpsDenominator,
        std::memory_order_release
    );


    const double videoFps =
        fpsDenominator != 0
        ? static_cast<double>(
            fpsNumerator
            ) /
        static_cast<double>(
            fpsDenominator
            )
        : 0.0;


    Logger::Log(
        "[CameraVideoPlayer] Decoder ready: %ux%u, stride=%ld, FPS=%.3f\n",
        width,
        height,
        sourceStride,
        videoFps
    );


    const size_t outputSize =
        static_cast<size_t>(width) *
        static_cast<size_t>(height) *
        4;


    /*
     * ---------------------------------------------------------
     * Decode loop
     * ---------------------------------------------------------
     */

    while (
        !m_stopRequested.load(
            std::memory_order_acquire
        )
        )
    {
        /*
         * Handle seek requests from the main thread.
         */
        double seekTime = 0.0;
        bool performSeek = false;


        {
            std::lock_guard<std::mutex>
                lock(m_queueMutex);


            if (m_seekRequested)
            {
                seekTime =
                    m_seekTime;

                m_seekRequested =
                    false;

                performSeek =
                    true;

                m_frameQueue.clear();

                m_endOfStream =
                    false;
            }
        }


        if (performSeek)
        {
            seekTime =
                (std::max)(
                    0.0,
                    seekTime
                    );


            PROPVARIANT position;

            PropVariantInit(
                &position
            );


            position.vt =
                VT_I8;

            position.hVal.QuadPart =
                static_cast<LONGLONG>(
                    seekTime *
                    static_cast<double>(
                        HNS_PER_SECOND
                        )
                    );


            hr =
                reader->SetCurrentPosition(
                    GUID_NULL,
                    position
                );


            PropVariantClear(
                &position
            );


            if (FAILED(hr))
            {
                Logger::Log(
                    "[CameraVideoPlayer] Seek failed: 0x%08X\n",
                    static_cast<unsigned>(hr)
                );

                m_decoderFailed.store(
                    true,
                    std::memory_order_release
                );

                break;
            }


            m_lastDecodedFrameTime.store(
                seekTime,
                std::memory_order_release
            );


            Logger::Log(
                "[CameraVideoPlayer] Decoder seek: %.6f\n",
                seekTime
            );
        }


        /*
         * If the queue is full, wait until the main thread
         * consumes a frame or requests a seek.
         */
        {
            std::unique_lock<std::mutex>
                lock(m_queueMutex);


            if (
                m_frameQueue.size() >=
                MAX_BUFFERED_FRAMES
                )
            {
                m_queueCondition.wait(
                    lock,
                    [&]
                    {
                        return
                            m_stopRequested.load(
                                std::memory_order_acquire
                            ) ||
                            m_seekRequested ||
                            m_frameQueue.size() <
                            MAX_BUFFERED_FRAMES;
                    }
                );
            }
        }


        if (
            m_stopRequested.load(
                std::memory_order_acquire
            )
            )
        {
            break;
        }


        /*
         * -----------------------------------------------------
         * Read one sample
         * -----------------------------------------------------
         */

        DWORD streamIndex = 0;
        DWORD flags = 0;

        LONGLONG timestamp = 0;

        ComPtr<IMFSample>
            sample;


        hr =
            reader->ReadSample(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                0,
                &streamIndex,
                &flags,
                &timestamp,
                &sample
            );


        if (FAILED(hr))
        {
            Logger::Log(
                "[CameraVideoPlayer] ReadSample failed: 0x%08X\n",
                static_cast<unsigned>(hr)
            );

            m_decoderFailed.store(
                true,
                std::memory_order_release
            );

            break;
        }


        if (
            flags &
            MF_SOURCE_READERF_ENDOFSTREAM
            )
        {
            {
                std::lock_guard<std::mutex>
                    lock(m_queueMutex);

                m_endOfStream =
                    true;
            }


            std::unique_lock<std::mutex>
                lock(m_queueMutex);


            m_queueCondition.wait(
                lock,
                [&]
                {
                    return
                        m_stopRequested.load(
                            std::memory_order_acquire
                        ) ||
                        m_seekRequested;
                }
            );


            continue;
        }


        if (
            flags &
            MF_SOURCE_READERF_STREAMTICK
            )
        {
            continue;
        }


        if (!sample)
        {
            continue;
        }


        /*
         * -----------------------------------------------------
         * Decode to CPU BGRA
         * -----------------------------------------------------
         */

        std::vector<uint8_t>
            frameBuffer;


        frameBuffer.resize(
            outputSize
        );


        const auto decodeStart =
            std::chrono::steady_clock::now();


        if (!DecodeSample(
            sample.Get(),
            sourceStride,
            static_cast<int>(width),
            static_cast<int>(height),
            frameBuffer
        ))
        {
            Logger::Log(
                "[CameraVideoPlayer] Failed to decode sample.\n"
            );

            m_decoderFailed.store(
                true,
                std::memory_order_release
            );

            break;
        }


        const auto decodeEnd =
            std::chrono::steady_clock::now();


        const double decodeFrameTimeMs =
            std::chrono::duration<double, std::milli>(
                decodeEnd - decodeStart
            ).count();


        m_decodeFrameTimeAccumulatorMs.fetch_add(
            decodeFrameTimeMs,
            std::memory_order_relaxed
        );

        const uint64_t decodeFrameCount =
            m_decodeStatFrameCount.fetch_add(
                1,
                std::memory_order_relaxed
            ) + 1;


        m_framesDecoded.fetch_add(
            1,
            std::memory_order_relaxed
        );


        m_lastDecodedFrameTime.store(
            static_cast<double>(timestamp) /
            static_cast<double>(HNS_PER_SECOND),
            std::memory_order_release
        );


        const double decodeStatElapsed =
            std::chrono::duration<double>(
                decodeEnd -
                m_decodeStatStart
            ).count();


        if (
            decodeStatElapsed >=
            STAT_INTERVAL_SECONDS
            )
        {
            const double accumulatedMs =
                m_decodeFrameTimeAccumulatorMs.exchange(
                    0.0,
                    std::memory_order_acq_rel
                );


            const uint64_t frameCount =
                m_decodeStatFrameCount.exchange(
                    0,
                    std::memory_order_acq_rel
                );


            if (frameCount > 0)
            {
                m_decodeFps.store(
                    static_cast<double>(
                        frameCount
                        ) /
                    decodeStatElapsed,
                    std::memory_order_release
                );

                m_decodeFrameTimeMs.store(
                    accumulatedMs /
                    static_cast<double>(
                        frameCount
                        ),
                    std::memory_order_release
                );
            }


            m_decodeStatStart =
                decodeEnd;
        }


        /*
         * -----------------------------------------------------
         * Put decoded frame into the queue
         * -----------------------------------------------------
         */

        DecodedFrame frame;

        frame.time =
            static_cast<double>(timestamp) /
            static_cast<double>(HNS_PER_SECOND);

        frame.pixels =
            std::move(
                frameBuffer
            );


        {
            std::lock_guard<std::mutex>
                lock(m_queueMutex);


            if (
                m_frameQueue.size() >=
                MAX_BUFFERED_FRAMES
                )
            {
                m_frameQueue.pop_front();

                m_framesDropped.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
            }


            m_frameQueue.emplace_back(
                std::move(frame)
            );
        }


        m_queueCondition.notify_one();
    }


    if (comInitialized)
    {
        CoUninitialize();
    }


    m_threadRunning.store(
        false,
        std::memory_order_release
    );


    Logger::Log(
        "[CameraVideoPlayer] Decoder thread stopped.\n"
    );
}


bool CameraVideoPlayer::DecodeSample(
    IMFSample* sample,
    LONG sourceStride,
    int width,
    int height,
    std::vector<uint8_t>& output
)
{
    if (
        !sample ||
        width <= 0 ||
        height <= 0
        )
    {
        return false;
    }


    const size_t rowBytes =
        static_cast<size_t>(width) *
        4;


    const size_t sourceRowBytes =
        static_cast<size_t>(
            std::abs(sourceStride)
            );


    const size_t requiredSize =
        sourceRowBytes *
        static_cast<size_t>(height);


    const size_t outputSize =
        rowBytes *
        static_cast<size_t>(height);


    if (
        output.size() <
        outputSize
        )
    {
        return false;
    }


    ComPtr<IMFMediaBuffer>
        buffer;


    HRESULT hr =
        sample->ConvertToContiguousBuffer(
            &buffer
        );


    if (FAILED(hr))
    {
        return false;
    }


    BYTE* data = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;


    hr =
        buffer->Lock(
            &data,
            &maxLength,
            &currentLength
        );


    if (FAILED(hr))
    {
        return false;
    }


    if (
        sourceRowBytes < rowBytes ||
        static_cast<size_t>(currentLength) <
        requiredSize
        )
    {
        buffer->Unlock();

        Logger::Log(
            "[CameraVideoPlayer] Decoded sample is too small.\n"
        );

        return false;
    }


    /*
     * Normalize RGB32 to tightly packed BGRA.
     *
     * The decoded alpha channel is forced to 255 because the
     * samples from this recording have zero alpha.
     */
    for (int y = 0; y < height; ++y)
    {
        const int sourceY =
            sourceStride >= 0
            ? y
            : (height - 1 - y);


        const uint8_t* source =
            data +
            static_cast<size_t>(sourceY) *
            sourceRowBytes;


        uint8_t* destination =
            output.data() +
            static_cast<size_t>(y) *
            rowBytes;


        std::memcpy(
            destination,
            source,
            rowBytes
        );


        for (int x = 0; x < width; ++x)
        {
            destination[
                static_cast<size_t>(x) * 4 +
                    3
            ] = 255;
        }
    }


    buffer->Unlock();


    return true;
}


bool CameraVideoPlayer::CreateTexture()
{
    if (!m_device)
    {
        return false;
    }


    const int width =
        m_width.load(
            std::memory_order_acquire
        );

    const int height =
        m_height.load(
            std::memory_order_acquire
        );


    if (
        width <= 0 ||
        height <= 0
        )
    {
        return false;
    }


    D3D11_TEXTURE2D_DESC description{};


    description.Width =
        static_cast<UINT>(width);

    description.Height =
        static_cast<UINT>(height);

    description.MipLevels =
        1;

    description.ArraySize =
        1;

    description.Format =
        DXGI_FORMAT_B8G8R8A8_UNORM;

    description.SampleDesc.Count =
        1;

    description.Usage =
        D3D11_USAGE_DEFAULT;

    description.BindFlags =
        D3D11_BIND_SHADER_RESOURCE;


    HRESULT hr =
        m_device->CreateTexture2D(
            &description,
            nullptr,
            &m_texture
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoPlayer] CreateTexture2D failed: 0x%08X\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    hr =
        m_device->CreateShaderResourceView(
            m_texture.Get(),
            nullptr,
            &m_shaderResourceView
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoPlayer] CreateShaderResourceView failed: 0x%08X\n",
            static_cast<unsigned>(hr)
        );

        m_texture.Reset();

        return false;
    }


    m_uploadBuffer.resize(
        static_cast<size_t>(width) *
        static_cast<size_t>(height) *
        4
    );


    Logger::Log(
        "[CameraVideoPlayer] Texture created: %dx%d\n",
        width,
        height
    );


    return true;
}


bool CameraVideoPlayer::RequestSeek(
    double time
)
{
    time =
        (std::max)(
            0.0,
            time
            );


    {
        std::lock_guard<std::mutex>
            lock(m_queueMutex);

        m_seekTime =
            time;

        m_seekRequested =
            true;

        m_frameQueue.clear();

        m_endOfStream =
            false;
    }


    m_currentFrameTime =
        time;


    m_targetTime.store(
        time,
        std::memory_order_release
    );


    m_queueCondition.notify_all();


    return true;
}


bool CameraVideoPlayer::Update(
    double time
)
{
    if (
        !m_open.load(
            std::memory_order_acquire
        )
        )
    {
        return false;
    }


    if (
        m_decoderFailed.load(
            std::memory_order_acquire
        )
        )
    {
        return false;
    }


    const double targetTime =
        (std::max)(
            0.0,
            time
            );


    m_targetTime.store(
        targetTime,
        std::memory_order_release
    );


    /*
     * ---------------------------------------------------------
     * Detect backwards movement of the Viewer clock.
     * ---------------------------------------------------------
     */

    if (
        targetTime + 0.000001 <
        m_currentFrameTime
        )
    {
        RequestSeek(
            targetTime
        );

        return true;
    }


    /*
     * Wake the decoder if it is currently waiting because its
     * queue is full.
     */
    m_queueCondition.notify_one();


    /*
     * ---------------------------------------------------------
     * Create D3D texture after the decoder discovers dimensions.
     * ---------------------------------------------------------
     */

    if (
        !m_texture &&
        GetWidth() > 0 &&
        GetHeight() > 0
        )
    {
        if (!CreateTexture())
        {
            Logger::Log(
                "[CameraVideoPlayer] Failed to create video texture.\n"
            );

            return false;
        }
    }


    if (!m_texture)
    {
        /*
         * Media Foundation is still starting up.
         */
        return true;
    }


    /*
     * ---------------------------------------------------------
     * Select the newest decoded frame <= targetTime.
     * ---------------------------------------------------------
     */

    bool haveFrame =
        false;


    double selectedTime =
        m_currentFrameTime;


    {
        std::lock_guard<std::mutex>
            lock(m_queueMutex);


        /*
         * Remove frames which are definitely older than the
         * newest usable frame.
         */
        while (
            m_frameQueue.size() >= 2 &&
            m_frameQueue[1].time <= targetTime
            )
        {
            m_frameQueue.pop_front();
        }


        if (
            !m_frameQueue.empty() &&
            m_frameQueue.front().time <= targetTime
            )
        {
            DecodedFrame frame =
                std::move(
                    m_frameQueue.front()
                );

            m_frameQueue.pop_front();


            m_uploadBuffer =
                std::move(
                    frame.pixels
                );

            selectedTime =
                frame.time;

            haveFrame =
                true;
        }
    }


    if (!haveFrame)
    {
        return true;
    }


    /*
     * ---------------------------------------------------------
     * GPU upload.
     * ---------------------------------------------------------
     */

    const auto uploadStart =
        std::chrono::steady_clock::now();


    const UINT rowPitch =
        static_cast<UINT>(
            GetWidth() * 4
            );


    m_context->UpdateSubresource(
        m_texture.Get(),
        0,
        nullptr,
        m_uploadBuffer.data(),
        rowPitch,
        0
    );


    const auto uploadEnd =
        std::chrono::steady_clock::now();


    const double uploadFrameTimeMs =
        std::chrono::duration<double, std::milli>(
            uploadEnd - uploadStart
        ).count();


    m_uploadFrameTimeAccumulatorMs.fetch_add(
        uploadFrameTimeMs,
        std::memory_order_relaxed
    );


    const uint64_t uploadFrameCount =
        m_uploadStatFrameCount.fetch_add(
            1,
            std::memory_order_relaxed
        ) + 1;


    m_framesUploaded.fetch_add(
        1,
        std::memory_order_relaxed
    );


    const double uploadStatElapsed =
        std::chrono::duration<double>(
            uploadEnd -
            m_uploadStatStart
        ).count();


    if (
        uploadStatElapsed >=
        STAT_INTERVAL_SECONDS
        )
    {
        const double accumulatedMs =
            m_uploadFrameTimeAccumulatorMs.exchange(
                0.0,
                std::memory_order_acq_rel
            );


        const uint64_t frameCount =
            m_uploadStatFrameCount.exchange(
                0,
                std::memory_order_acq_rel
            );


        if (frameCount > 0)
        {
            m_uploadFps.store(
                static_cast<double>(
                    frameCount
                    ) /
                uploadStatElapsed,
                std::memory_order_release
            );

            m_uploadFrameTimeMs.store(
                accumulatedMs /
                static_cast<double>(
                    frameCount
                    ),
                std::memory_order_release
            );
        }


        m_uploadStatStart =
            uploadEnd;
    }


    m_currentFrameTime =
        selectedTime;


    /*
     * The decoder may now have room for another frame.
     */
    m_queueCondition.notify_one();


    return true;
}


CameraVideoPlayerStatistics
CameraVideoPlayer::GetStatistics() const
{
    CameraVideoPlayerStatistics statistics{};


    statistics.width =
        m_width.load(
            std::memory_order_acquire
        );

    statistics.height =
        m_height.load(
            std::memory_order_acquire
        );


    statistics.fpsNumerator =
        m_fpsNumerator.load(
            std::memory_order_acquire
        );

    statistics.fpsDenominator =
        m_fpsDenominator.load(
            std::memory_order_acquire
        );


    if (
        statistics.fpsDenominator != 0
        )
    {
        statistics.videoFps =
            static_cast<double>(
                statistics.fpsNumerator
                ) /
            static_cast<double>(
                statistics.fpsDenominator
                );
    }


    statistics.decodeFps =
        m_decodeFps.load(
            std::memory_order_acquire
        );

    statistics.decodeFrameTimeMs =
        m_decodeFrameTimeMs.load(
            std::memory_order_acquire
        );


    statistics.uploadFps =
        m_uploadFps.load(
            std::memory_order_acquire
        );

    statistics.uploadFrameTimeMs =
        m_uploadFrameTimeMs.load(
            std::memory_order_acquire
        );


    statistics.framesDecoded =
        m_framesDecoded.load(
            std::memory_order_acquire
        );

    statistics.framesUploaded =
        m_framesUploaded.load(
            std::memory_order_acquire
        );

    statistics.framesDropped =
        m_framesDropped.load(
            std::memory_order_acquire
        );


    statistics.frameBytes =
        static_cast<size_t>(
            statistics.width
            ) *
        static_cast<size_t>(
            statistics.height
            ) *
        4;


    {
        std::lock_guard<std::mutex>
            lock(m_queueMutex);


        statistics.queuedFrames =
            m_frameQueue.size();


        if (
            m_frameQueue.size() >= 2
            )
        {
            const double firstTime =
                m_frameQueue.front().time;

            const double lastTime =
                m_frameQueue.back().time;


            statistics.bufferedTimeMs =
                (lastTime - firstTime) *
                1000.0;
        }
    }


    statistics.currentFrameTime =
        m_currentFrameTime;


    statistics.targetTime =
        m_targetTime.load(
            std::memory_order_acquire
        );


    const double decodedTime =
        m_lastDecodedFrameTime.load(
            std::memory_order_acquire
        );


    statistics.decoderLeadMs =
        (
            decodedTime -
            statistics.targetTime
            ) *
        1000.0;


    statistics.open =
        m_open.load(
            std::memory_order_acquire
        );

    statistics.threadRunning =
        m_threadRunning.load(
            std::memory_order_acquire
        );


    {
        std::lock_guard<std::mutex>
            lock(m_queueMutex);

        statistics.endOfStream =
            m_endOfStream;
    }


    return statistics;
}