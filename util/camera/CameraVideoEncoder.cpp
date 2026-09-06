#include "CameraVideoEncoder.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <cstring>

#include "../../../util/Logger.h"


namespace
{

    /*
     * Convert a UTF-8 std::string into a Windows UTF-16 string.
     *
     * This is important for paths containing characters such as
     * å, ä, ö, é, Chinese characters, etc.
     */
    std::wstring UTF8ToWide(
        const std::string& string
    )
    {
        if (string.empty())
        {
            return {};
        }


        const int requiredSize =
            MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                string.data(),
                static_cast<int>(string.size()),
                nullptr,
                0
            );


        if (requiredSize <= 0)
        {
            /*
             * Fall back to the system ANSI code page if the input
             * wasn't actually UTF-8.
             */
            const int fallbackSize =
                MultiByteToWideChar(
                    CP_ACP,
                    0,
                    string.data(),
                    static_cast<int>(string.size()),
                    nullptr,
                    0
                );


            if (fallbackSize <= 0)
            {
                return {};
            }


            std::wstring result(
                static_cast<size_t>(fallbackSize),
                L'\0'
            );


            MultiByteToWideChar(
                CP_ACP,
                0,
                string.data(),
                static_cast<int>(string.size()),
                result.data(),
                fallbackSize
            );


            return result;
        }


        std::wstring result(
            static_cast<size_t>(requiredSize),
            L'\0'
        );


        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            string.data(),
            static_cast<int>(string.size()),
            result.data(),
            requiredSize
        );


        return result;
    }

}


CameraVideoEncoder::CameraVideoEncoder()
{}


CameraVideoEncoder::~CameraVideoEncoder()
{
    Stop();
}


bool CameraVideoEncoder::Start(
    const std::string& filePath,
    int width,
    int height,
    int fps,
    PixelFormat format
)
{
    if (m_recording)
    {
        Logger::Log(
            "[Recording] Already recording.\n"
        );

        return false;
    }


    if (
        filePath.empty() ||
        width <= 0 ||
        height <= 0 ||
        fps <= 0
        )
    {
        Logger::Log(
            "[Recording] Invalid recording parameters.\n"
        );

        return false;
    }


    const std::wstring widePath =
        UTF8ToWide(filePath);


    if (widePath.empty())
    {
        Logger::Log(
            "[Recording] Failed to convert file path to UTF-16.\n"
        );

        return false;
    }


    m_width =
        width;

    m_height =
        height;

    m_fps =
        fps;

    m_format =
        format;


    if (!CreateEncoder(
        widePath,
        format
    ))
    {
        m_sinkWriter.Reset();

        m_width = 0;
        m_height = 0;
        m_fps = 0;

        return false;
    }


    m_recording = true;


    Logger::Log(
        "[Recording] Started: %s (%dx%d @ %d FPS, %s)\n",
        filePath.c_str(),
        m_width,
        m_height,
        m_fps,
        m_format == PixelFormat::NV12
        ? "NV12"
        : "RGB32"
    );


    return true;
}


bool CameraVideoEncoder::CreateEncoder(
    const std::wstring& filePath,
    PixelFormat format
)
{
    HRESULT hr;


    // ============================================================
    // Create sink writer
    // ============================================================

    Microsoft::WRL::ComPtr<IMFAttributes>
        attributes;


    hr =
        MFCreateAttributes(
            &attributes,
            1
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] MFCreateAttributes failed: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    /*
     * Disable throttling.
     *
     * This allows the sink writer to process frames without
     * artificially limiting the application to realtime speed.
     */
    hr =
        attributes->SetUINT32(
            MF_SINK_WRITER_DISABLE_THROTTLING,
            TRUE
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] Failed to disable throttling: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    hr =
        MFCreateSinkWriterFromURL(
            filePath.c_str(),
            nullptr,
            attributes.Get(),
            &m_sinkWriter
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] MFCreateSinkWriterFromURL failed: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    // ============================================================
    // Output media type
    // ============================================================

    Microsoft::WRL::ComPtr<IMFMediaType>
        outputType;


    hr =
        MFCreateMediaType(
            &outputType
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] MFCreateMediaType failed for output: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    hr =
        outputType->SetGUID(
            MF_MT_MAJOR_TYPE,
            MFMediaType_Video
        );


    if (FAILED(hr))
        return false;


    hr =
        outputType->SetGUID(
            MF_MT_SUBTYPE,
            MFVideoFormat_H264
        );


    if (FAILED(hr))
        return false;


    hr =
        outputType->SetUINT32(
            MF_MT_AVG_BITRATE,
            8'000'000
        );


    if (FAILED(hr))
        return false;


    hr =
        MFSetAttributeSize(
            outputType.Get(),
            MF_MT_FRAME_SIZE,
            static_cast<UINT32>(m_width),
            static_cast<UINT32>(m_height)
        );


    if (FAILED(hr))
        return false;


    hr =
        MFSetAttributeRatio(
            outputType.Get(),
            MF_MT_FRAME_RATE,
            static_cast<UINT32>(m_fps),
            1
        );


    if (FAILED(hr))
        return false;


    hr =
        MFSetAttributeRatio(
            outputType.Get(),
            MF_MT_PIXEL_ASPECT_RATIO,
            1,
            1
        );


    if (FAILED(hr))
        return false;


    /*
     * Progressive video.
     */
    hr =
        outputType->SetUINT32(
            MF_MT_INTERLACE_MODE,
            MFVideoInterlace_Progressive
        );


    if (FAILED(hr))
        return false;


    hr =
        m_sinkWriter->AddStream(
            outputType.Get(),
            &m_videoStreamIndex
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] AddStream failed: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    // ============================================================
    // Input media type
    // ============================================================

    Microsoft::WRL::ComPtr<IMFMediaType>
        inputType;


    hr =
        MFCreateMediaType(
            &inputType
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] MFCreateMediaType failed for input: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    hr =
        inputType->SetGUID(
            MF_MT_MAJOR_TYPE,
            MFMediaType_Video
        );


    if (FAILED(hr))
        return false;


    /*
     * This is the important part:
     *
     * NV12 camera frame:
     *     MFVideoFormat_NV12
     *
     * MJPG camera frame after WIC decoding:
     *     MFVideoFormat_RGB32
     */
    hr =
        inputType->SetGUID(
            MF_MT_SUBTYPE,
            format == PixelFormat::NV12
            ? MFVideoFormat_NV12
            : MFVideoFormat_RGB32
        );


    if (FAILED(hr))
        return false;


    hr =
        MFSetAttributeSize(
            inputType.Get(),
            MF_MT_FRAME_SIZE,
            static_cast<UINT32>(m_width),
            static_cast<UINT32>(m_height)
        );


    if (FAILED(hr))
        return false;


    hr =
        MFSetAttributeRatio(
            inputType.Get(),
            MF_MT_FRAME_RATE,
            static_cast<UINT32>(m_fps),
            1
        );


    if (FAILED(hr))
        return false;


    hr =
        MFSetAttributeRatio(
            inputType.Get(),
            MF_MT_PIXEL_ASPECT_RATIO,
            1,
            1
        );


    if (FAILED(hr))
        return false;


    hr =
        inputType->SetUINT32(
            MF_MT_INTERLACE_MODE,
            MFVideoInterlace_Progressive
        );


    if (FAILED(hr))
        return false;


    /*
     * RGB32 is tightly packed at 4 bytes per pixel.
     *
     * NV12 is width * height * 3 / 2 bytes.
     *
     * The sink writer understands the standard layouts from
     * the subtype, so no manual stride attribute is necessary.
     */
    hr =
        m_sinkWriter->SetInputMediaType(
            m_videoStreamIndex,
            inputType.Get(),
            nullptr
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] SetInputMediaType failed: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    // ============================================================
    // Begin writing
    // ============================================================

    hr =
        m_sinkWriter->BeginWriting();


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] BeginWriting failed: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    return true;
}


bool CameraVideoEncoder::EncodeFrame(
    const uint8_t* pixels,
    size_t pixelSize,
    int64_t timestamp100ns
)
{
    if (
        !m_recording ||
        !m_sinkWriter ||
        !pixels
        )
    {
        return false;
    }


    // ============================================================
    // Determine expected frame size
    // ============================================================

    size_t expectedSize = 0;


    if (m_format == PixelFormat::NV12)
    {
        /*
         * NV12 consists of:
         *
         * Y:
         *     width * height
         *
         * UV:
         *     width * height / 2
         *
         * Total:
         *     width * height * 3 / 2
         */
        expectedSize =
            static_cast<size_t>(m_width) *
            static_cast<size_t>(m_height) *
            3 /
            2;
    }
    else
    {
        /*
         * RGB32 / BGRA:
         *
         * 4 bytes per pixel.
         */
        expectedSize =
            static_cast<size_t>(m_width) *
            static_cast<size_t>(m_height) *
            4;
    }


    if (pixelSize < expectedSize)
    {
        Logger::Log(
            "[CameraVideoEncoder] Frame too small. "
            "Expected=%llu Actual=%llu\n",
            static_cast<unsigned long long>(expectedSize),
            static_cast<unsigned long long>(pixelSize)
        );

        return false;
    }


    // ============================================================
    // Create Media Foundation buffer
    // ============================================================

    Microsoft::WRL::ComPtr<IMFMediaBuffer>
        buffer;


    HRESULT hr =
        MFCreateMemoryBuffer(
            static_cast<DWORD>(expectedSize),
            &buffer
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] MFCreateMemoryBuffer failed: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    BYTE* destination =
        nullptr;

    DWORD maxLength =
        0;

    DWORD currentLength =
        0;


    hr =
        buffer->Lock(
            &destination,
            &maxLength,
            &currentLength
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] Buffer Lock failed: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    // ============================================================
    // Copy frame
    // ============================================================

    std::memcpy(
        destination,
        pixels,
        expectedSize
    );


    buffer->Unlock();


    hr =
        buffer->SetCurrentLength(
            static_cast<DWORD>(expectedSize)
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] SetCurrentLength failed: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    // ============================================================
    // Create sample
    // ============================================================

    Microsoft::WRL::ComPtr<IMFSample>
        sample;


    hr =
        MFCreateSample(
            &sample
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] MFCreateSample failed: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    hr =
        sample->AddBuffer(
            buffer.Get()
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] AddBuffer failed: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    // ============================================================
    // Timestamp
    // ============================================================

    hr =
        sample->SetSampleTime(
            timestamp100ns
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] SetSampleTime failed: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    const LONGLONG frameDuration =
        10'000'000LL /
        static_cast<LONGLONG>(m_fps);


    hr =
        sample->SetSampleDuration(
            frameDuration
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] SetSampleDuration failed: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    // ============================================================
    // Write sample
    // ============================================================

    hr =
        m_sinkWriter->WriteSample(
            m_videoStreamIndex,
            sample.Get()
        );


    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoEncoder] WriteSample failed: 0x%08X.\n",
            static_cast<unsigned>(hr)
        );

        return false;
    }


    return true;
}


void CameraVideoEncoder::Stop()
{
    if (!m_sinkWriter)
    {
        m_recording = false;
        return;
    }


    if (m_recording)
    {
        Logger::Log(
            "[CameraVideoEncoder] Finalizing video.\n"
        );


        const HRESULT hr =
            m_sinkWriter->Finalize();


        if (FAILED(hr))
        {
            Logger::Log(
                "[CameraVideoEncoder] Finalize failed: 0x%08X.\n",
                static_cast<unsigned>(hr)
            );
        }
    }


    m_sinkWriter.Reset();


    m_recording = false;


    m_videoStreamIndex = 0;

    m_width = 0;

    m_height = 0;

    m_fps = 0;
}


bool CameraVideoEncoder::IsRecording() const
{
    return m_recording;
}