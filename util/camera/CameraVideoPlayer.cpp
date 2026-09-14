#include "CameraVideoPlayer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

#include "../../../util/Logger.h"


namespace
{
    inline uint8_t ClampByte(
        int value
    )
    {
        if (value < 0)
            return 0;

        if (value > 255)
            return 255;

        return static_cast<uint8_t>(value);
    }


    bool DecodeNV12Sample(
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
            height <= 0 ||
            (width & 1) != 0 ||
            (height & 1) != 0
            )
        {
            return false;
        }


        const size_t rowBytes =
            static_cast<size_t>(width) *
            4;

        const size_t outputSize =
            rowBytes *
            static_cast<size_t>(height);


        if (output.size() < outputSize)
        {
            return false;
        }


        ComPtr<IMFMediaBuffer>
            buffer;


        HRESULT hr =
            sample->GetBufferByIndex(
                0,
                &buffer
            );


        if (FAILED(hr))
        {
            return false;
        }


        BYTE* scanline0 = nullptr;
        BYTE* bufferStart = nullptr;
        LONG pitch = 0;
        DWORD bufferLength = 0;


        bool use2DBuffer = false;


        ComPtr<IMF2DBuffer2>
            buffer2D;


        if (
            SUCCEEDED(
                buffer.As(
                    &buffer2D
                )
            )
            )
        {
            hr =
                buffer2D->Lock2DSize(
                    MF2DBuffer_LockFlags_Read,
                    &scanline0,
                    &pitch,
                    &bufferStart,
                    &bufferLength
                );

            if (SUCCEEDED(hr))
            {
                use2DBuffer = true;
            }
        }


        if (use2DBuffer)
        {
            const size_t absPitch =
                static_cast<size_t>(
                    std::abs(pitch)
                    );


            if (
                absPitch <
                static_cast<size_t>(width)
                )
            {
                buffer2D->Unlock2D();
                return false;
            }


            /*
             * NV12 is laid out as:
             *
             *   Y plane:  surfaceHeight rows
             *   UV plane: surfaceHeight / 2 rows
             *
             * A decoded video surface can have a height aligned above
             * the visible media height (for example 1088 storage rows
             * for a 1080 video). The UV plane therefore must not
             * necessarily begin at `pitch * height`.
             *
             * Infer the actual surface height from the 2-D buffer size
             * and pitch. This fixes the case where the first aligned
             * rows of the surface were being interpreted as UV data,
             * which produces the green strip at the top of the image.
             */
            size_t surfaceHeight =
                static_cast<size_t>(height);


            if (
                bufferStart &&
                bufferLength > 0
                )
            {
                const size_t totalRows =
                    static_cast<size_t>(bufferLength) /
                    absPitch;

                if (
                    totalRows >=
                    static_cast<size_t>(height) +
                    static_cast<size_t>(height / 2)
                    &&
                    (totalRows * 2) % 3 == 0
                    )
                {
                    const size_t inferredHeight =
                        (totalRows * 2) / 3;

                    if (
                        inferredHeight >=
                        static_cast<size_t>(height) &&
                        (inferredHeight & 1) == 0
                        )
                    {
                        surfaceHeight =
                            inferredHeight;
                    }
                }
            }


            const size_t requiredBytes =
                absPitch *
                (
                    surfaceHeight +
                    surfaceHeight / 2
                    );


            if (
                !bufferStart ||
                static_cast<size_t>(bufferLength) <
                requiredBytes
                )
            {
                buffer2D->Unlock2D();
                return false;
            }


            /*
             * NV12 camera surfaces used by this player are expected to
             * be top-down. For a negative pitch, the UV plane placement
             * relative to scanline0 is ambiguous for a generic 2-D
             * surface, so fall back to the contiguous path below.
             */
            if (pitch > 0)
            {
                const uint8_t* yPlane =
                    scanline0;

                const uint8_t* uvPlane =
                    bufferStart +
                    absPitch * surfaceHeight;


                for (int y = 0; y < height; ++y)
                {
                    const uint8_t* yRow =
                        yPlane +
                        static_cast<size_t>(y) *
                        absPitch;

                    const uint8_t* uvRow =
                        uvPlane +
                        static_cast<size_t>(y / 2) *
                        absPitch;

                    uint8_t* destination =
                        output.data() +
                        static_cast<size_t>(y) *
                        rowBytes;


                    for (int x = 0; x < width; x += 2)
                    {
                        const int u =
                            static_cast<int>(uvRow[x]) -
                            128;

                        const int v =
                            static_cast<int>(uvRow[x + 1]) -
                            128;


                        for (int dx = 0; dx < 2; ++dx)
                        {
                            const int pixelX =
                                x + dx;

                            const int yValue =
                                static_cast<int>(
                                    yRow[pixelX]
                                    );

                            const int c =
                                yValue -
                                16;

                            const int r =
                                (298 * c + 409 * v + 128) >>
                                8;

                            const int g =
                                (298 * c - 100 * u - 208 * v + 128) >>
                                8;

                            const int b =
                                (298 * c + 516 * u + 128) >>
                                8;

                            uint8_t* pixel =
                                destination +
                                static_cast<size_t>(pixelX) *
                                4;

                            pixel[0] = ClampByte(b);
                            pixel[1] = ClampByte(g);
                            pixel[2] = ClampByte(r);
                            pixel[3] = 255;
                        }
                    }
                }


                buffer2D->Unlock2D();
                return true;
            }


            buffer2D->Unlock2D();
        }


        /*
         * Fallback for buffers that do not expose IMF2DBuffer2, or
         * for a negative-pitch surface. A contiguous NV12 buffer uses
         * the packed layout with the Y plane followed by the UV plane.
         */
        ComPtr<IMFMediaBuffer>
            contiguousBuffer;


        hr =
            sample->ConvertToContiguousBuffer(
                &contiguousBuffer
            );


        if (FAILED(hr))
        {
            return false;
        }


        BYTE* data = nullptr;
        DWORD maxLength = 0;
        DWORD currentLength = 0;


        hr =
            contiguousBuffer->Lock(
                &data,
                &maxLength,
                &currentLength
            );


        if (FAILED(hr))
        {
            return false;
        }


        const size_t stride =
            sourceStride > 0
            ? static_cast<size_t>(sourceStride)
            : static_cast<size_t>(width);


        const size_t requiredSize =
            stride *
            (
                static_cast<size_t>(height) +
                static_cast<size_t>(height / 2)
                );


        if (
            static_cast<size_t>(currentLength) <
            requiredSize
            )
        {
            contiguousBuffer->Unlock();

            Logger::Log(
                "[CameraVideoPlayer] NV12 contiguous sample is too small: %u < %zu.\n",
                currentLength,
                requiredSize
            );

            return false;
        }


        const uint8_t* yPlane =
            data;

        const uint8_t* uvPlane =
            data +
            stride *
            static_cast<size_t>(height);


        for (int y = 0; y < height; ++y)
        {
            const uint8_t* yRow =
                yPlane +
                static_cast<size_t>(y) *
                stride;

            const uint8_t* uvRow =
                uvPlane +
                static_cast<size_t>(y / 2) *
                stride;

            uint8_t* destination =
                output.data() +
                static_cast<size_t>(y) *
                rowBytes;


            for (int x = 0; x < width; x += 2)
            {
                const int u =
                    static_cast<int>(uvRow[x]) -
                    128;

                const int v =
                    static_cast<int>(uvRow[x + 1]) -
                    128;


                for (int dx = 0; dx < 2; ++dx)
                {
                    const int pixelX =
                        x + dx;

                    const int yValue =
                        static_cast<int>(
                            yRow[pixelX]
                            );

                    const int c =
                        yValue -
                        16;

                    const int r =
                        (298 * c + 409 * v + 128) >>
                        8;

                    const int g =
                        (298 * c - 100 * u - 208 * v + 128) >>
                        8;

                    const int b =
                        (298 * c + 516 * u + 128) >>
                        8;

                    uint8_t* pixel =
                        destination +
                        static_cast<size_t>(pixelX) *
                        4;

                    pixel[0] = ClampByte(b);
                    pixel[1] = ClampByte(g);
                    pixel[2] = ClampByte(r);
                    pixel[3] = 255;
                }
            }
        }


        contiguousBuffer->Unlock();
        return true;
    }
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


    m_seekInProgress.store(
        false,
        std::memory_order_release
    );


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


    m_seekInProgress.store(
        false,
        std::memory_order_release
    );


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
     * Prefer the native decoded NV12 format. This avoids asking
     * Media Foundation to convert every frame to RGB32.
     */
    hr =
        attributes->SetUINT32(
            MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,
            FALSE
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoPlayer] Failed to disable video processing: 0x%08X\n",
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
     * Prefer NV12 output
     * ---------------------------------------------------------
     */

    PixelFormat
        pixelFormat =
        PixelFormat::NV12;

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
            MFVideoFormat_NV12
        );

    if (SUCCEEDED(hr))
    {
        hr =
            reader->SetCurrentMediaType(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                nullptr,
                outputType.Get()
            );
    }

    /*
     * Some sources may not expose NV12 through the decoder.
     * Fall back to RGB32 with Media Foundation video processing.
     */
    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoPlayer] NV12 output unavailable: 0x%08X, falling back to RGB32.\n",
            static_cast<unsigned>(hr)
        );

        ComPtr<IMFAttributes>
            fallbackAttributes;

        hr =
            MFCreateAttributes(
                &fallbackAttributes,
                2
            );

        if (SUCCEEDED(hr))
        {
            hr =
                fallbackAttributes->SetUINT32(
                    MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,
                    TRUE
                );
        }

        ComPtr<IMFSourceReader>
            fallbackReader;

        if (SUCCEEDED(hr))
        {
            hr =
                MFCreateSourceReaderFromURL(
                    widePath.c_str(),
                    fallbackAttributes.Get(),
                    &fallbackReader
                );
        }

        if (FAILED(hr))
        {
            Logger::Log(
                "[CameraVideoPlayer] Failed to create RGB32 fallback reader: 0x%08X\n",
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

        ComPtr<IMFMediaType>
            fallbackType;

        hr =
            MFCreateMediaType(
                &fallbackType
            );

        if (SUCCEEDED(hr))
        {
            hr =
                fallbackType->SetGUID(
                    MF_MT_MAJOR_TYPE,
                    MFMediaType_Video
                );
        }

        if (SUCCEEDED(hr))
        {
            hr =
                fallbackType->SetGUID(
                    MF_MT_SUBTYPE,
                    MFVideoFormat_RGB32
                );
        }

        if (SUCCEEDED(hr))
        {
            hr =
                fallbackReader->SetCurrentMediaType(
                    MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                    nullptr,
                    fallbackType.Get()
                );
        }

        if (FAILED(hr))
        {
            Logger::Log(
                "[CameraVideoPlayer] Failed to set RGB32 fallback output format: 0x%08X\n",
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

        reader =
            std::move(
                fallbackReader
            );

        pixelFormat =
            PixelFormat::RGB32;
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

    GUID currentSubtype{};

    hr =
        currentType->GetGUID(
            MF_MT_SUBTYPE,
            &currentSubtype
        );

    if (SUCCEEDED(hr))
    {
        if (
            IsEqualGUID(
                currentSubtype,
                MFVideoFormat_NV12
            )
            )
        {
            pixelFormat =
                PixelFormat::NV12;
        }
        else if (
            IsEqualGUID(
                currentSubtype,
                MFVideoFormat_RGB32
            )
            )
        {
            pixelFormat =
                PixelFormat::RGB32;
        }
        else
        {
            Logger::Log(
                "[CameraVideoPlayer] Unsupported output subtype.\n"
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
        pixelFormat == PixelFormat::NV12
        ? static_cast<LONG>(width)
        : static_cast<LONG>(width * 4);

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

    const char*
        pixelFormatName =
        pixelFormat == PixelFormat::NV12
        ? "NV12"
        : "RGB32";

    Logger::Log(
        "[CameraVideoPlayer] Decoder ready: %ux%u, stride=%ld, FPS=%.3f, format=%s\n",
        width,
        height,
        sourceStride,
        videoFps,
        pixelFormatName
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
                /*
                 * A failed seek must NOT terminate the decoder.
                 * Close() is the only operation that should stop it.
                 */
                Logger::Log(
                    "[CameraVideoPlayer] Seek failed: 0x%08X. Keeping decoder thread alive.\n",
                    static_cast<unsigned>(hr)
                );

                /*
                 * Leave the decoder alive and wait for another seek or
                 * an explicit Close(). Do not mark it as failed.
                 */
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

            m_seekInProgress.store(
                true,
                std::memory_order_release
            );
        }

        /*
         * ---------------------------------------------------------
         * Decoder pacing
         * ---------------------------------------------------------
         */
        if (!performSeek)
        {
            std::unique_lock<std::mutex>
                lock(m_queueMutex);

            m_queueCondition.wait(
                lock,
                [&]
                {
                    if (
                        m_stopRequested.load(
                            std::memory_order_acquire
                        ) ||
                        m_seekRequested
                        )
                    {
                        return true;
                    }

                    const double currentTarget =
                        m_targetTime.load(
                            std::memory_order_acquire
                        );

                    const double currentDecoded =
                        m_lastDecodedFrameTime.load(
                            std::memory_order_acquire
                        );

                    return
                        currentDecoded <
                        currentTarget +
                        m_maxDecoderBuffer;
                }
            );
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



        {
            double decodedFrameTime =
                static_cast<double>(timestamp) /
                static_cast<double>(HNS_PER_SECOND);

            while (
                performSeek && 
                decodedFrameTime <= seekTime
                ) 
            {

                sample.Reset();

                hr = reader->ReadSample(
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

                if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
                    break;

                decodedFrameTime =
                    static_cast<double>(timestamp) /
                    static_cast<double>(HNS_PER_SECOND);
            }
        }

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

        const double decodedFrameTime =
            static_cast<double>(timestamp) /
            static_cast<double>(HNS_PER_SECOND);

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

            /*
             * EOF is NOT a reason to stop the decoder thread.
             *
             * Keep the thread alive and wait until either:
             *  - Close() requests shutdown, or
             *  - a new seek is requested.
             */
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

        bool decodeSucceeded =
            false;

        if (
            pixelFormat ==
            PixelFormat::NV12
            )
        {
            decodeSucceeded =
                DecodeNV12Sample(
                    sample.Get(),
                    sourceStride,
                    static_cast<int>(width),
                    static_cast<int>(height),
                    frameBuffer
                );
        }
        else
        {
            decodeSucceeded =
                DecodeSample(
                    sample.Get(),
                    sourceStride,
                    static_cast<int>(width),
                    static_cast<int>(height),
                    frameBuffer
                );
        }

        if (!decodeSucceeded)
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
            decodedFrameTime,
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
            "[CameraVideoPlayer] Decoded RGB32 sample is too small.\n"
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


bool CameraVideoPlayer::Seek(double time)
{
    return RequestSeek(time);
}


bool CameraVideoPlayer::RequestSeek(
    double time
)
{
    Logger::Log(
        "Seek Requested: %.2f\n",
        time
    );


    time =
        (std::max)(
            0.0,
            time
            );


    bool canUseQueuedFrame =
        false;


    {
        std::lock_guard<std::mutex>
            lock(m_queueMutex);


        /*
            * If the requested time is already covered by the decoded
            * queue, there is no reason to clear it or ask Media
            * Foundation to seek.
            *
            * Update() will select the appropriate frame from the queue
            * on its next call.
            */
        if (
            !m_frameQueue.empty() &&
            time >= m_frameQueue.front().time &&
            time <= m_frameQueue.back().time
            )
        {
            canUseQueuedFrame =
                true;
        }


        m_seekTime =
            time;


        m_endOfStream =
            false;


        if (canUseQueuedFrame)
        {
            m_seekRequested =
                false;
        }
        else
        {
            m_seekRequested =
                true;

            m_frameQueue.clear();
        }
    }


    m_targetTime.store(
        time,
        std::memory_order_release
    );


    /*
        * A queued-frame seek is already satisfied. The requested
        * timestamp is inside our decoded range, so Update() can simply
        * present the appropriate frame from the queue.
        */
    m_seekInProgress.store(
        !canUseQueuedFrame,
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
        * Detect a discontinuous movement of the playback clock.
        * ---------------------------------------------------------
        *
        * Normal playback differences are small. A larger jump means
        * the decoder should be repositioned instead of decoding all
        * intermediate frames.
        * ---------------------------------------------------------
        */

    const double frameDelta =
        targetTime -
        m_currentFrameTime;


    if (
        !m_seekInProgress.load(
            std::memory_order_acquire
        ) &&
        std::abs(frameDelta) >
        SEEK_THRESHOLD_SECONDS
        )
    {
        RequestSeek(
            targetTime
        );

        return true;
    }


    /*
        * ---------------------------------------------------------
        * Wake the decoder.
        * ---------------------------------------------------------
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
            * Remove frames for which a newer frame is already
            * available without passing the target time.
            */
        while (
            m_frameQueue.size() >= 2 &&
            m_frameQueue[1].time <= targetTime
            )
        {
            m_frameQueue.pop_front();
        }


        /*
            * Use the newest frame we currently have that does not
            * exceed the playback position.
            */
        const bool seekInProgress =
            m_seekInProgress.load(
                std::memory_order_acquire
            );


        if (
            !m_frameQueue.empty() &&
            (
                m_frameQueue.front().time <= targetTime ||
                seekInProgress
                )
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


    /*
        * This is now the actual frame on the GPU.
        */
    m_currentFrameTime =
        selectedTime;


    /*
        * A seek is considered recovered once the decoder has reached
        * the current target and that data has been presented.
        *
        * The decoder may have reached the target before this frame
        * was uploaded, so keep the state active until this point.
        */
    const double decodedTime =
        m_lastDecodedFrameTime.load(
            std::memory_order_acquire
        );


    const double currentTarget =
        m_targetTime.load(
            std::memory_order_acquire
        );


    if (
        m_seekInProgress.load(
            std::memory_order_acquire
        ) &&
        decodedTime >= currentTarget
        )
    {
        m_seekInProgress.store(
            false,
            std::memory_order_release
        );
    }


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
