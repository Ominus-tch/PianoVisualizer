#include "CameraVideoPlayer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

#include "../../../util/Logger.h"


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

    m_device = device;
    m_context = context;

    return true;
}


void CameraVideoPlayer::Shutdown()
{
    Close();

    m_device = nullptr;
    m_context = nullptr;
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

    const std::wstring widePath =
        path.wstring();

    // ---------------------------------------------------------
    // Source reader attributes
    // ---------------------------------------------------------

    ComPtr<IMFAttributes> attributes;

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

        return false;
    }

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

        return false;
    }

    // ---------------------------------------------------------
    // Open MP4
    // ---------------------------------------------------------

    hr =
        MFCreateSourceReaderFromURL(
            widePath.c_str(),
            attributes.Get(),
            &m_reader
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoPlayer] Failed to open video: 0x%08X\n",
            static_cast<unsigned>(hr)
        );

        m_reader.Reset();

        return false;
    }

    // ---------------------------------------------------------
    // Request RGB32 output
    // ---------------------------------------------------------

    ComPtr<IMFMediaType> outputType;

    hr =
        MFCreateMediaType(
            &outputType
        );

    if (FAILED(hr))
    {
        Close();
        return false;
    }

    hr =
        outputType->SetGUID(
            MF_MT_MAJOR_TYPE,
            MFMediaType_Video
        );

    if (FAILED(hr))
    {
        Close();
        return false;
    }

    hr =
        outputType->SetGUID(
            MF_MT_SUBTYPE,
            MFVideoFormat_RGB32
        );

    if (FAILED(hr))
    {
        Close();
        return false;
    }

    hr =
        m_reader->SetCurrentMediaType(
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

        Close();

        return false;
    }

    // ---------------------------------------------------------
    // Read actual output media type
    // ---------------------------------------------------------

    ComPtr<IMFMediaType> currentType;

    hr =
        m_reader->GetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            &currentType
        );

    if (FAILED(hr))
    {
        Close();
        return false;
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

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraVideoPlayer] Failed to get video dimensions.\n"
        );

        Close();

        return false;
    }

    m_width =
        static_cast<int>(width);

    m_height =
        static_cast<int>(height);

    // ---------------------------------------------------------
    // Determine source stride
    // ---------------------------------------------------------

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
        m_sourceStride =
            static_cast<LONG>(strideUnsigned);
    }
    else
    {
        m_sourceStride =
            static_cast<LONG>(
                width * 4
                );
    }

    if (m_sourceStride == 0)
    {
        m_sourceStride =
            static_cast<LONG>(
                width * 4
                );
    }

    // ---------------------------------------------------------
    // Create D3D11 texture
    // ---------------------------------------------------------

    if (!CreateTexture())
    {
        Close();
        return false;
    }

    Logger::Log(
        "[CameraVideoPlayer] Texture created: %dx%d\n",
        m_width,
        m_height
    );

    // ---------------------------------------------------------
    // Decode first frame
    // ---------------------------------------------------------

    ComPtr<IMFSample> sample;

    LONGLONG timestamp = 0;

    if (!ReadNextSample(
        sample,
        timestamp
    ))
    {
        Logger::Log(
            "[CameraVideoPlayer] Failed to decode first frame.\n"
        );

        Close();

        return false;
    }

    Logger::Log(
        "[CameraVideoPlayer] First frame timestamp: %.6f\n",
        static_cast<double>(timestamp) / 10'000'000.0
    );

    if (!UploadSample(sample.Get()))
    {
        Logger::Log(
            "[CameraVideoPlayer] Failed to upload first frame.\n"
        );

        Close();

        return false;
    }

    Logger::Log(
        "[CameraVideoPlayer] First frame uploaded successfully.\n"
    );

    m_currentFrameTime =
        static_cast<double>(timestamp) /
        10'000'000.0;

    m_filePath =
        filePath;

    m_open = true;

    Logger::Log(
        "[CameraVideoPlayer] Open complete. SRV=%p\n",
        m_shaderResourceView.Get()
    );

    Logger::Log(
        "[CameraVideoPlayer] Opened: %s (%dx%d)\n",
        filePath.c_str(),
        m_width,
        m_height
    );

    return true;
}


void CameraVideoPlayer::Close()
{
    m_pendingSample.Reset();

    m_reader.Reset();

    m_shaderResourceView.Reset();
    m_texture.Reset();

    m_frameBuffer.clear();

    m_width = 0;
    m_height = 0;
    m_sourceStride = 0;

    m_currentFrameTime = 0.0;

    m_pendingTimestamp = 0;

    m_endOfStream = false;
    m_open = false;

    m_filePath.clear();
}


bool CameraVideoPlayer::CreateTexture()
{
    D3D11_TEXTURE2D_DESC description{};

    description.Width =
        static_cast<UINT>(m_width);

    description.Height =
        static_cast<UINT>(m_height);

    description.MipLevels = 1;
    description.ArraySize = 1;

    description.Format =
        DXGI_FORMAT_B8G8R8A8_UNORM;

    description.SampleDesc.Count = 1;

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

    m_frameBuffer.resize(
        static_cast<size_t>(m_width) *
        static_cast<size_t>(m_height) *
        4
    );

    return true;
}

bool CameraVideoPlayer::ReadNextSample(
    ComPtr<IMFSample>& sample,
    LONGLONG& timestamp
)
{
    sample.Reset();
    timestamp = 0;

    if (!m_reader)
    {
        return false;
    }

    while (true)
    {
        DWORD streamIndex = 0;
        DWORD flags = 0;

        HRESULT hr =
            m_reader->ReadSample(
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

            return false;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            m_endOfStream = true;
            return false;
        }

        if (flags & MF_SOURCE_READERF_STREAMTICK)
        {
            continue;
        }

        if (!sample)
        {
            continue;
        }

        return true;
    }
}


bool CameraVideoPlayer::UploadSample(
    IMFSample* sample
)
{
    if (
        !sample ||
        !m_texture ||
        !m_context
        )
    {
        return false;
    }

    ComPtr<IMFMediaBuffer> buffer;

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

    const size_t rowBytes =
        static_cast<size_t>(m_width) * 4;

    const size_t sourceStride =
        static_cast<size_t>(
            std::abs(m_sourceStride)
            );

    const size_t requiredSize =
        sourceStride *
        static_cast<size_t>(m_height);

    if (
        sourceStride < rowBytes ||
        static_cast<size_t>(currentLength) < requiredSize
        )
    {
        buffer->Unlock();

        Logger::Log(
            "[CameraVideoPlayer] Decoded sample is too small.\n"
        );

        return false;
    }

    // Normalize the source to tightly-packed BGRA.
    for (int y = 0; y < m_height; ++y)
    {
        const int sourceY =
            m_sourceStride >= 0
            ? y
            : (m_height - 1 - y);

        const uint8_t* source =
            data +
            static_cast<size_t>(sourceY) *
            sourceStride;

        uint8_t* destination =
            m_frameBuffer.data() +
            static_cast<size_t>(y) *
            rowBytes;

        std::memcpy(
            destination,
            source,
            rowBytes
        );

        for (int x = 0; x < m_width; ++x)
        {
            destination[
                static_cast<size_t>(x) * 4 + 3
            ] = 255;
        }
    }

    buffer->Unlock();

    m_context->UpdateSubresource(
        m_texture.Get(),
        0,
        nullptr,
        m_frameBuffer.data(),
        static_cast<UINT>(rowBytes),
        0
    );

    return true;
}


bool CameraVideoPlayer::Seek(
    double time
)
{
    if (!m_reader)
    {
        return false;
    }

    time =
        (std::max)(0.0, time);

    PROPVARIANT position;
    PropVariantInit(
        &position
    );

    position.vt =
        VT_I8;

    position.hVal.QuadPart =
        static_cast<LONGLONG>(
            time * 10'000'000.0
            );

    HRESULT hr =
        m_reader->SetCurrentPosition(
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

        return false;
    }

    m_pendingSample.Reset();
    m_pendingTimestamp = 0;
    m_endOfStream = false;

    ComPtr<IMFSample> sample;

    LONGLONG timestamp = 0;

    if (!ReadNextSample(
        sample,
        timestamp
    ))
    {
        return false;
    }

    if (!UploadSample(
        sample.Get()
    ))
    {
        return false;
    }

    m_currentFrameTime =
        static_cast<double>(timestamp) /
        10'000'000.0;

    return true;
}


bool CameraVideoPlayer::Update(
    double time
)
{
    if (!m_open)
    {
        return false;
    }

    const double targetTime =
        (std::max)(0.0, time);

    // ---------------------------------------------------------
    // Time moved backwards
    // ---------------------------------------------------------

    if (
        targetTime + 0.000001 <
        m_currentFrameTime
        )
    {
        return Seek(targetTime);
    }

    // ---------------------------------------------------------
    // Advance through frames
    // ---------------------------------------------------------

    while (true)
    {
        ComPtr<IMFSample> sample;

        LONGLONG timestamp = 0;

        if (m_pendingSample)
        {
            sample =
                m_pendingSample;

            timestamp =
                m_pendingTimestamp;

            m_pendingSample.Reset();
            m_pendingTimestamp = 0;
        }
        else
        {
            if (m_endOfStream)
            {
                return true;
            }

            if (!ReadNextSample(
                sample,
                timestamp
            ))
            {
                return true;
            }
        }

        const double frameTime =
            static_cast<double>(timestamp) /
            10'000'000.0;

        if (frameTime > targetTime)
        {
            m_pendingSample =
                sample;

            m_pendingTimestamp =
                timestamp;

            return true;
        }

        if (!UploadSample(
            sample.Get()
        ))
        {
            return false;
        }

        m_currentFrameTime =
            frameTime;
    }
}