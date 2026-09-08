#include "Camera.h"

#include <Windows.h>
#include <wincodec.h>
#include <d3dcompiler.h>

#include "../Logger.h"

#include <cstring>
#include <cstdint>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3dcompiler.lib")

#define LOG_UPLOAD(step) \
    Logger::Log("UploadFrame: " step "\n")

#define LOG_UPLOAD_HR(step, hr) \
    Logger::Log( \
        std::string("UploadFrame: " step " HRESULT=0x") + \
        std::to_string(static_cast<unsigned long>(hr)) + \
        "\n" \
    )

#ifndef MF_E_NO_MORE_TYPES
#define MF_E_NO_MORE_TYPES ((HRESULT)0xC00D36B9L)
#endif


// =========================================================
// Construction
// =========================================================

Camera::Camera()
{}


// =========================================================
// Destruction
// =========================================================

Camera::~Camera()
{
    Shutdown();
}


// =========================================================
// Initialize
// =========================================================

bool Camera::Initialize(
    ID3D11Device* device,
    ID3D11DeviceContext* context
)
{
    if (!device || !context)
    {
        Logger::Log(
            "[Camera] Invalid D3D11 device/context.\n"
        );

        return false;
    }

    m_device = device;
    m_context = context;

    m_cameraIndex = -1;

    // -----------------------------------------------------
    // Initialize Media Foundation
    // -----------------------------------------------------

    HRESULT hr =
        MFStartup(
            MF_VERSION,
            MFSTARTUP_FULL
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] MFStartup failed. HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        m_device = nullptr;
        m_context = nullptr;

        return false;
    }

    m_mediaFoundationStarted = true;

    // -----------------------------------------------------
    // Initialize WIC
    // -----------------------------------------------------

    hr =
        CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&m_wicFactory)
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to create WIC imaging factory. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        Shutdown();

        return false;
    }

    Logger::Log(
        "[Camera] WIC initialized.\n"
    );

    // -----------------------------------------------------
    // Enumerate cameras
    // -----------------------------------------------------

    if (!EnumerateCameras())
    {
        Logger::Log(
            "[Camera] Failed to enumerate cameras.\n"
        );

        Shutdown();

        return false;
    }

    if (m_availableCameras.empty())
    {
        Logger::Log(
            "[Camera] No cameras found.\n"
        );

		m_lastError = "Could not find any cameras on this system.";

        return true;
    }

    // -----------------------------------------------------
    // Automatically open the only camera
    // -----------------------------------------------------

    if (m_availableCameras.size() == 1)
    {
        if (!OpenCamera(0))
        {
            Logger::Log(
                "[Camera] Failed to open the only available camera.\n"
            );
        }
    }
    else
    {
        Logger::Log(
            "[Camera] Multiple cameras found. Waiting for user selection.\n"
        );
    }

    return true;
}

// =========================================================
// FindCamera
// =========================================================

bool Camera::FindCamera(
    int cameraIndex,
    IMFActivate** camera
)
{
    if (!camera)
        return false;

    *camera = nullptr;

    // -----------------------------------------------------
    // Create attributes
    // -----------------------------------------------------

    Microsoft::WRL::ComPtr<IMFAttributes> attributes;

    HRESULT hr =
        MFCreateAttributes(
            &attributes,
            1
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] MFCreateAttributes failed. HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        return false;
    }

    hr =
        attributes->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to set video capture attribute.\n"
        );

        return false;
    }

    // -----------------------------------------------------
    // Enumerate cameras
    // -----------------------------------------------------

    IMFActivate** devices = nullptr;
    UINT32 count = 0;

    hr =
        MFEnumDeviceSources(
            attributes.Get(),
            &devices,
            &count
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] MFEnumDeviceSources failed. HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        return false;
    }

    Logger::Log(
        "[Camera] Found %u camera(s).\n",
        count
    );

    if (count == 0)
    {
        CoTaskMemFree(devices);

        return false;
    }

    if (
        cameraIndex < 0 ||
        static_cast<UINT32>(cameraIndex) >= count
        )
    {
        Logger::Log(
            "[Camera] Invalid camera index: %d.\n",
            cameraIndex
        );

        for (UINT32 i = 0; i < count; ++i)
        {
            if (devices[i])
                devices[i]->Release();
        }

        CoTaskMemFree(devices);

        return false;
    }

    *camera =
        devices[cameraIndex];

    Logger::Log(
        "[Camera] Using camera index %d.\n",
        cameraIndex
    );

    // -----------------------------------------------------
    // Release all other devices
    // -----------------------------------------------------

    for (UINT32 i = 0; i < count; ++i)
    {
        if (i != static_cast<UINT32>(cameraIndex))
        {
            if (devices[i])
                devices[i]->Release();
        }
    }

    CoTaskMemFree(devices);

    return true;
}


// =========================================================
// CreateReader
// =========================================================

bool Camera::CreateReader(
    IMFActivate* camera,
    CameraFormat requestedFormat
)
{
    if (!camera)
        return false;

    if (
        requestedFormat != CameraFormat::NV12 &&
        requestedFormat != CameraFormat::MJPG
        )
    {
        return false;
    }

    // -----------------------------------------------------
    // Activate media source
    // -----------------------------------------------------

    Microsoft::WRL::ComPtr<IMFMediaSource> source;

    HRESULT hr =
        camera->ActivateObject(
            IID_PPV_ARGS(&source)
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to activate camera. HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        return false;
    }

    m_controls.Initialize(
        source.Get()
    );

    // -----------------------------------------------------
    // Source reader attributes
    // -----------------------------------------------------

    Microsoft::WRL::ComPtr<IMFAttributes> attributes;

    hr =
        MFCreateAttributes(
            &attributes,
            1
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to create reader attributes. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        return false;
    }

    hr =
        attributes->SetUINT32(
            MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,
            FALSE
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to configure source reader.\n"
        );

        return false;
    }

    // -----------------------------------------------------
    // Create source reader
    // -----------------------------------------------------

    hr =
        MFCreateSourceReaderFromMediaSource(
            source.Get(),
            attributes.Get(),
            &m_reader
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] MFCreateSourceReaderFromMediaSource failed. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        return false;
    }

    // -----------------------------------------------------
    // Requested subtype
    // -----------------------------------------------------

    GUID requestedSubtype{};

    if (requestedFormat == CameraFormat::NV12)
    {
        requestedSubtype =
            MFVideoFormat_NV12;
    }
    else
    {
        requestedSubtype =
            MFVideoFormat_MJPG;
    }

    // -----------------------------------------------------
    // Enumerate native formats
    //
    // Store every supported mode for this subtype.
    //
    // If m_requestedMode matches one of them exactly,
    // that mode will be selected.
    //
    // Otherwise:
    //   1. Highest resolution wins.
    //   2. Highest FPS breaks resolution ties.
    // -----------------------------------------------------

    m_availableModes.clear();

    Microsoft::WRL::ComPtr<IMFMediaType> bestType;

    CameraMode bestMode{};

    UINT64 bestPixels = 0;

    Microsoft::WRL::ComPtr<IMFMediaType> requestedType;

    CameraMode requestedMode{};

    bool requestedModeFound = false;

    for (DWORD index = 0;; ++index)
    {
        Microsoft::WRL::ComPtr<IMFMediaType> type;

        hr =
            m_reader->GetNativeMediaType(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                index,
                &type
            );

        if (hr == MF_E_NO_MORE_TYPES)
            break;

        if (FAILED(hr))
            break;

        // -------------------------------------------------
        // Check subtype
        // -------------------------------------------------

        GUID subtype{};

        if (
            FAILED(
                type->GetGUID(
                    MF_MT_SUBTYPE,
                    &subtype
                )
            )
            )
        {
            continue;
        }

        if (subtype != requestedSubtype)
            continue;

        // -------------------------------------------------
        // Get resolution
        // -------------------------------------------------

        UINT32 width = 0;
        UINT32 height = 0;

        if (
            FAILED(
                MFGetAttributeSize(
                    type.Get(),
                    MF_MT_FRAME_SIZE,
                    &width,
                    &height
                )
            )
            )
        {
            continue;
        }

        // -------------------------------------------------
        // Get FPS
        // -------------------------------------------------

        UINT32 fpsNumerator = 0;
        UINT32 fpsDenominator = 1;

        MFGetAttributeRatio(
            type.Get(),
            MF_MT_FRAME_RATE,
            &fpsNumerator,
            &fpsDenominator
        );

        if (fpsDenominator == 0)
            fpsDenominator = 1;

        // -------------------------------------------------
        // Build mode
        // -------------------------------------------------

        CameraMode mode{};

        mode.width =
            static_cast<int>(width);

        mode.height =
            static_cast<int>(height);

        mode.fpsNumerator =
            fpsNumerator;

        mode.fpsDenominator =
            fpsDenominator;

        // -------------------------------------------------
        // Avoid duplicate modes
        // -------------------------------------------------

        bool duplicate = false;

        for (const CameraMode& existing : m_availableModes)
        {
            if (
                existing.width ==
                mode.width &&
                existing.height ==
                mode.height &&
                existing.fpsNumerator ==
                mode.fpsNumerator &&
                existing.fpsDenominator ==
                mode.fpsDenominator
                )
            {
                duplicate = true;
                break;
            }
        }

        if (duplicate)
            continue;

        // -------------------------------------------------
        // Store available mode
        // -------------------------------------------------

        m_availableModes.push_back(
            mode
        );

        // -------------------------------------------------
        // Check if this is the requested mode
        // -------------------------------------------------

        const bool isRequested =
            m_requestedMode.width ==
            mode.width &&
            m_requestedMode.height ==
            mode.height &&
            m_requestedMode.fpsNumerator ==
            mode.fpsNumerator &&
            m_requestedMode.fpsDenominator ==
            mode.fpsDenominator;

        if (isRequested)
        {
            requestedType =
                type;

            requestedMode =
                mode;

            requestedModeFound = true;
        }

        // -------------------------------------------------
        // Determine fallback best mode
        // -------------------------------------------------

        const UINT64 pixels =
            static_cast<UINT64>(width) *
            static_cast<UINT64>(height);

        bool better = false;

        if (!bestType)
        {
            better = true;
        }
        else if (pixels > bestPixels)
        {
            better = true;
        }
        else if (
            pixels == bestPixels &&
            static_cast<UINT64>(fpsNumerator) *
            static_cast<UINT64>(bestMode.fpsDenominator) >
            static_cast<UINT64>(bestMode.fpsNumerator) *
            static_cast<UINT64>(fpsDenominator)
            )
        {
            better = true;
        }

        if (better)
        {
            bestType =
                type;

            bestMode =
                mode;

            bestPixels =
                pixels;
        }
    }

    // -----------------------------------------------------
    // Make sure at least one mode exists
    // -----------------------------------------------------

    if (!bestType)
    {
        Logger::Log(
            requestedFormat == CameraFormat::NV12
            ? "[Camera] No NV12 camera format found.\n"
            : "[Camera] No MJPG camera format found.\n"
        );

        m_availableModes.clear();

        m_reader.Reset();

        return false;
    }

    // -----------------------------------------------------
    // Select requested mode if available.
    //
    // Otherwise use best mode.
    // -----------------------------------------------------

    Microsoft::WRL::ComPtr<IMFMediaType> selectedType;

    CameraMode selectedMode{};

    if (requestedModeFound)
    {
        selectedType =
            requestedType;

        selectedMode =
            requestedMode;

        Logger::Log(
            requestedFormat == CameraFormat::NV12
            ? "[Camera] Requested NV12 mode found: %dx%d @ %u/%u FPS\n"
            : "[Camera] Requested MJPG mode found: %dx%d @ %u/%u FPS\n",
            selectedMode.width,
            selectedMode.height,
            selectedMode.fpsNumerator,
            selectedMode.fpsDenominator
        );
    }
    else
    {
        selectedType =
            bestType;

        selectedMode =
            bestMode;

        Logger::Log(
            requestedFormat == CameraFormat::NV12
            ? "[Camera] Requested NV12 mode unavailable. Using best mode: %dx%d @ %u/%u FPS\n"
            : "[Camera] Requested MJPG mode unavailable. Using best mode: %dx%d @ %u/%u FPS\n",
            selectedMode.width,
            selectedMode.height,
            selectedMode.fpsNumerator,
            selectedMode.fpsDenominator
        );
    }

    // -----------------------------------------------------
    // Select format
    // -----------------------------------------------------

    hr =
        m_reader->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            nullptr,
            selectedType.Get()
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to set camera format. HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        m_reader.Reset();

        return false;
    }

    // -----------------------------------------------------
    // Get actual format
    // -----------------------------------------------------

    Microsoft::WRL::ComPtr<IMFMediaType> actualType;

    hr =
        m_reader->GetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            &actualType
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to get actual camera format. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        m_reader.Reset();

        return false;
    }

    GUID actualSubtype{};

    hr =
        actualType->GetGUID(
            MF_MT_SUBTYPE,
            &actualSubtype
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to get actual camera subtype.\n"
        );

        m_reader.Reset();

        return false;
    }

    // -----------------------------------------------------
    // Verify actual subtype
    // -----------------------------------------------------

    if (actualSubtype != requestedSubtype)
    {
        Logger::Log(
            "[Camera] Camera returned an unexpected subtype.\n"
        );

        m_reader.Reset();

        return false;
    }

    // -----------------------------------------------------
    // Actual dimensions
    // -----------------------------------------------------

    UINT32 actualWidth = 0;
    UINT32 actualHeight = 0;

    hr =
        MFGetAttributeSize(
            actualType.Get(),
            MF_MT_FRAME_SIZE,
            &actualWidth,
            &actualHeight
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to get frame size.\n"
        );

        m_reader.Reset();

        return false;
    }

    // -----------------------------------------------------
    // Actual FPS
    // -----------------------------------------------------

    UINT32 actualFpsNumerator = 0;
    UINT32 actualFpsDenominator = 1;

    MFGetAttributeRatio(
        actualType.Get(),
        MF_MT_FRAME_RATE,
        &actualFpsNumerator,
        &actualFpsDenominator
    );

    if (actualFpsDenominator == 0)
        actualFpsDenominator = 1;

    // -----------------------------------------------------
    // Store camera properties
    // -----------------------------------------------------

    m_width =
        static_cast<int>(actualWidth);

    m_height =
        static_cast<int>(actualHeight);

    m_fpsNumerator =
        actualFpsNumerator;

    m_fpsDenominator =
        actualFpsDenominator;

    m_format =
        requestedFormat;

    // -----------------------------------------------------
    // Store the actual selected mode
    //
    // This is important because the camera/driver can
    // negotiate a slightly different mode than requested.
    // -----------------------------------------------------

    m_requestedMode.width =
        m_width;

    m_requestedMode.height =
        m_height;

    m_requestedMode.fpsNumerator =
        m_fpsNumerator;

    m_requestedMode.fpsDenominator =
        m_fpsDenominator;

    // -----------------------------------------------------
    // Calculate frame buffer sizes
    // -----------------------------------------------------

    if (m_format == CameraFormat::NV12)
    {
        m_nv12YSize =
            static_cast<size_t>(m_width) *
            static_cast<size_t>(m_height);

        m_nv12UVSize =
            m_nv12YSize / 2;

        m_frameBufferSize =
            m_nv12YSize +
            m_nv12UVSize;
    }
    else
    {
        m_nv12YSize = 0;
        m_nv12UVSize = 0;

        m_frameBufferSize =
            GetExpectedBGRASize();
    }

    // -----------------------------------------------------
    // Log available modes
    // -----------------------------------------------------

    Logger::Log(
        "[Camera] Available %s modes: %zu\n",
        requestedFormat == CameraFormat::NV12
        ? "NV12"
        : "MJPG",
        m_availableModes.size()
    );

    // -----------------------------------------------------
    // Log actual format
    // -----------------------------------------------------

    Logger::Log(
        "[Camera] Actual format: %dx%d @ %u/%u FPS (%s)\n",
        m_width,
        m_height,
        m_fpsNumerator,
        m_fpsDenominator,
        m_format == CameraFormat::NV12
        ? "NV12"
        : "MJPG"
    );

    Logger::Log(
        "[Camera] Frame buffer size: %zu bytes.\n",
        m_frameBufferSize
    );

    return true;
}

// =========================================================
// Camera Settings
// =========================================================



// =========================================================
// CreateTexture
// =========================================================

bool Camera::CreateTexture()
{
    if (!m_device)
        return false;

    if (
        m_width <= 0 ||
        m_height <= 0
        )
    {
        return false;
    }

    // -----------------------------------------------------
    // Final BGRA texture
    // -----------------------------------------------------

    if (!CreateBGRATexture())
        return false;

    // -----------------------------------------------------
    // NV12 resources
    // -----------------------------------------------------

    if (m_format == CameraFormat::NV12)
    {
        if (!CreateNV12Textures())
        {
            Logger::Log(
                "[Camera] Failed to create NV12 textures.\n"
            );

            m_renderTargetView.Reset();
            m_shaderResourceView.Reset();
            m_texture.Reset();

            return false;
        }

        if (!CreateNV12ConversionResources())
        {
            Logger::Log(
                "[Camera] Failed to create NV12 conversion resources.\n"
            );

            ReleaseNV12Resources();

            m_renderTargetView.Reset();
            m_shaderResourceView.Reset();
            m_texture.Reset();

            return false;
        }
    }

    return true;
}


// =========================================================
// CreateBGRATexture
// =========================================================

bool Camera::CreateBGRATexture()
{
    if (!m_device)
        return false;

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
        D3D11_BIND_SHADER_RESOURCE |
        D3D11_BIND_RENDER_TARGET;

    description.CPUAccessFlags = 0;

    HRESULT hr =
        m_device->CreateTexture2D(
            &description,
            nullptr,
            &m_texture
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to create BGRA texture. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        return false;
    }

    // -----------------------------------------------------
    // Shader resource view
    // -----------------------------------------------------

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDescription{};

    srvDescription.Format =
        DXGI_FORMAT_B8G8R8A8_UNORM;

    srvDescription.ViewDimension =
        D3D11_SRV_DIMENSION_TEXTURE2D;

    srvDescription.Texture2D.MostDetailedMip =
        0;

    srvDescription.Texture2D.MipLevels =
        1;

    hr =
        m_device->CreateShaderResourceView(
            m_texture.Get(),
            &srvDescription,
            &m_shaderResourceView
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to create BGRA shader resource view. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        m_texture.Reset();

        return false;
    }

    // -----------------------------------------------------
    // Render target view
    // -----------------------------------------------------

    D3D11_RENDER_TARGET_VIEW_DESC rtvDescription{};

    rtvDescription.Format =
        DXGI_FORMAT_B8G8R8A8_UNORM;

    rtvDescription.ViewDimension =
        D3D11_RTV_DIMENSION_TEXTURE2D;

    rtvDescription.Texture2D.MipSlice =
        0;

    hr =
        m_device->CreateRenderTargetView(
            m_texture.Get(),
            &rtvDescription,
            &m_renderTargetView
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to create BGRA render target view. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        m_shaderResourceView.Reset();
        m_texture.Reset();

        return false;
    }

    Logger::Log(
        "[Camera] Created %dx%d BGRA D3D11 texture.\n",
        m_width,
        m_height
    );

    return true;
}


// =========================================================
// CreateNV12Textures
// =========================================================

bool Camera::CreateNV12Textures()
{
    if (!m_device)
        return false;

    if (m_width <= 0 || m_height <= 0)
        return false;

    // -----------------------------------------------------
    // Y texture
    //
    // DEFAULT usage is intentional.
    //
    // CPU uploads use UpdateSubresource().
    // -----------------------------------------------------

    D3D11_TEXTURE2D_DESC yDescription{};

    yDescription.Width =
        static_cast<UINT>(m_width);

    yDescription.Height =
        static_cast<UINT>(m_height);

    yDescription.MipLevels = 1;
    yDescription.ArraySize = 1;

    yDescription.Format =
        DXGI_FORMAT_R8_UNORM;

    yDescription.SampleDesc.Count = 1;

    yDescription.Usage =
        D3D11_USAGE_DEFAULT;

    yDescription.BindFlags =
        D3D11_BIND_SHADER_RESOURCE;

    yDescription.CPUAccessFlags = 0;

    HRESULT hr =
        m_device->CreateTexture2D(
            &yDescription,
            nullptr,
            &m_nv12YTexture
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to create NV12 Y texture. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        return false;
    }

    // -----------------------------------------------------
    // Y SRV
    // -----------------------------------------------------

    D3D11_SHADER_RESOURCE_VIEW_DESC ySRVDescription{};

    ySRVDescription.Format =
        DXGI_FORMAT_R8_UNORM;

    ySRVDescription.ViewDimension =
        D3D11_SRV_DIMENSION_TEXTURE2D;

    ySRVDescription.Texture2D.MostDetailedMip =
        0;

    ySRVDescription.Texture2D.MipLevels =
        1;

    hr =
        m_device->CreateShaderResourceView(
            m_nv12YTexture.Get(),
            &ySRVDescription,
            &m_nv12YSRV
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to create NV12 Y SRV. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        m_nv12YTexture.Reset();

        return false;
    }

    // -----------------------------------------------------
    // UV texture
    //
    // NV12 chroma plane:
    //
    //     width  = width / 2
    //     height = height / 2
    //     format = R8G8
    // -----------------------------------------------------

    D3D11_TEXTURE2D_DESC uvDescription{};

    uvDescription.Width =
        static_cast<UINT>(m_width / 2);

    uvDescription.Height =
        static_cast<UINT>(m_height / 2);

    uvDescription.MipLevels = 1;
    uvDescription.ArraySize = 1;

    uvDescription.Format =
        DXGI_FORMAT_R8G8_UNORM;

    uvDescription.SampleDesc.Count = 1;

    uvDescription.Usage =
        D3D11_USAGE_DEFAULT;

    uvDescription.BindFlags =
        D3D11_BIND_SHADER_RESOURCE;

    uvDescription.CPUAccessFlags = 0;

    hr =
        m_device->CreateTexture2D(
            &uvDescription,
            nullptr,
            &m_nv12UVTexture
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to create NV12 UV texture. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        m_nv12YSRV.Reset();
        m_nv12YTexture.Reset();

        return false;
    }

    // -----------------------------------------------------
    // UV SRV
    // -----------------------------------------------------

    D3D11_SHADER_RESOURCE_VIEW_DESC uvSRVDescription{};

    uvSRVDescription.Format =
        DXGI_FORMAT_R8G8_UNORM;

    uvSRVDescription.ViewDimension =
        D3D11_SRV_DIMENSION_TEXTURE2D;

    uvSRVDescription.Texture2D.MostDetailedMip =
        0;

    uvSRVDescription.Texture2D.MipLevels =
        1;

    hr =
        m_device->CreateShaderResourceView(
            m_nv12UVTexture.Get(),
            &uvSRVDescription,
            &m_nv12UVSRV
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to create NV12 UV SRV. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        m_nv12UVTexture.Reset();
        m_nv12YSRV.Reset();
        m_nv12YTexture.Reset();

        return false;
    }

    // -----------------------------------------------------
    // The call above needs the output parameter.
    //
    // Recreate correctly here.
    // -----------------------------------------------------

    m_nv12UVSRV.Reset();

    hr =
        m_device->CreateShaderResourceView(
            m_nv12UVTexture.Get(),
            &uvSRVDescription,
            &m_nv12UVSRV
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to create NV12 UV SRV. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        m_nv12UVTexture.Reset();
        m_nv12YSRV.Reset();
        m_nv12YTexture.Reset();

        return false;
    }

    Logger::Log(
        "[Camera] Created NV12 textures.\n"
    );

    return true;
}


// =========================================================
// CreateNV12ConversionResources
// =========================================================

bool Camera::CreateNV12ConversionResources()
{
    if (!CreateNV12ConversionShaders())
        return false;

    // -----------------------------------------------------
    // Linear sampler
    // -----------------------------------------------------

    D3D11_SAMPLER_DESC samplerDescription{};

    samplerDescription.Filter =
        D3D11_FILTER_MIN_MAG_MIP_LINEAR;

    samplerDescription.AddressU =
        D3D11_TEXTURE_ADDRESS_CLAMP;

    samplerDescription.AddressV =
        D3D11_TEXTURE_ADDRESS_CLAMP;

    samplerDescription.AddressW =
        D3D11_TEXTURE_ADDRESS_CLAMP;

    samplerDescription.MipLODBias =
        0.0f;

    samplerDescription.MaxAnisotropy =
        1;

    samplerDescription.ComparisonFunc =
        D3D11_COMPARISON_NEVER;

    samplerDescription.MinLOD =
        0.0f;

    samplerDescription.MaxLOD =
        D3D11_FLOAT32_MAX;

    HRESULT hr =
        m_device->CreateSamplerState(
            &samplerDescription,
            &m_nv12Sampler
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to create NV12 sampler. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        return false;
    }

    return true;
}


// =========================================================
// CreateNV12ConversionShaders
// =========================================================

bool Camera::CreateNV12ConversionShaders()
{
    static const char* vertexShaderSource = R"(
        struct VSOutput
        {
            float4 position : SV_Position;
            float2 uv       : TEXCOORD0;
        };

        VSOutput main(uint vertexID : SV_VertexID)
        {
            VSOutput output;

            float2 positions[3];

            positions[0] = float2(-1.0, -1.0);
            positions[1] = float2(-1.0,  3.0);
            positions[2] = float2( 3.0, -1.0);

            float2 position =
                positions[vertexID];

            output.position =
                float4(
                    position,
                    0.0,
                    1.0
                );

            output.uv =
                float2(
                    (position.x + 1.0) * 0.5,
                    1.0 - (position.y + 1.0) * 0.5
                );

            return output;
        }
    )";

    static const char* pixelShaderSource = R"(
        Texture2D<float> yTexture : register(t0);
        Texture2D<float2> uvTexture : register(t1);

        SamplerState linearSampler : register(s0);

        struct PSInput
        {
            float4 position : SV_Position;
            float2 uv       : TEXCOORD0;
        };

        float4 main(PSInput input) : SV_Target
        {
            float y =
                yTexture.Sample(
                    linearSampler,
                    input.uv
                );

            float2 chroma =
                uvTexture.Sample(
                    linearSampler,
                    input.uv
                );

            float u =
                chroma.x - 0.5;

            float v =
                chroma.y - 0.5;

            float r =
                y +
                1.402 * v;

            float g =
                y -
                0.344136 * u -
                0.714136 * v;

            float b =
                y +
                1.772 * u;

            return float4(
                saturate(r),
                saturate(g),
                saturate(b),
                1.0
            );
        }
    )";

    // -----------------------------------------------------
    // Compile vertex shader
    // -----------------------------------------------------

    Microsoft::WRL::ComPtr<ID3DBlob> vertexErrors;

    HRESULT hr =
        D3DCompile(
            vertexShaderSource,
            std::strlen(vertexShaderSource),
            "CameraNV12VertexShader",
            nullptr,
            nullptr,
            "main",
            "vs_5_0",
            0,
            0,
            &m_nv12VertexShaderBlob,
            &vertexErrors
        );

    if (FAILED(hr))
    {
        if (vertexErrors)
        {
            Logger::Log(
                "[Camera] NV12 vertex shader compile error: %s\n",
                static_cast<const char*>(
                    vertexErrors->GetBufferPointer()
                    )
            );
        }

        return false;
    }

    // -----------------------------------------------------
    // Create vertex shader
    // -----------------------------------------------------

    hr =
        m_device->CreateVertexShader(
            m_nv12VertexShaderBlob->GetBufferPointer(),
            m_nv12VertexShaderBlob->GetBufferSize(),
            nullptr,
            &m_nv12VertexShader
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to create NV12 vertex shader. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        return false;
    }

    // -----------------------------------------------------
    // Compile pixel shader
    // -----------------------------------------------------

    Microsoft::WRL::ComPtr<ID3DBlob> pixelErrors;

    hr =
        D3DCompile(
            pixelShaderSource,
            std::strlen(pixelShaderSource),
            "CameraNV12PixelShader",
            nullptr,
            nullptr,
            "main",
            "ps_5_0",
            0,
            0,
            &m_nv12PixelShaderBlob,
            &pixelErrors
        );

    if (FAILED(hr))
    {
        if (pixelErrors)
        {
            Logger::Log(
                "[Camera] NV12 pixel shader compile error: %s\n",
                static_cast<const char*>(
                    pixelErrors->GetBufferPointer()
                    )
            );
        }

        return false;
    }

    // -----------------------------------------------------
    // Create pixel shader
    // -----------------------------------------------------

    hr =
        m_device->CreatePixelShader(
            m_nv12PixelShaderBlob->GetBufferPointer(),
            m_nv12PixelShaderBlob->GetBufferSize(),
            nullptr,
            &m_nv12PixelShader
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to create NV12 pixel shader. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        return false;
    }

    Logger::Log(
        "[Camera] NV12 conversion shaders created.\n"
    );

    return true;
}


// =========================================================
// ReleaseNV12Resources
// =========================================================

void Camera::ReleaseNV12Resources()
{
    m_nv12Sampler.Reset();

    m_nv12PixelShader.Reset();
    m_nv12VertexShader.Reset();

    m_nv12PixelShaderBlob.Reset();
    m_nv12VertexShaderBlob.Reset();

    m_nv12UVSRV.Reset();
    m_nv12UVTexture.Reset();

    m_nv12YSRV.Reset();
    m_nv12YTexture.Reset();
}


// =========================================================
// CameraThread
// =========================================================

void Camera::CameraThread()
{
    Logger::Log(
        "[Camera] Capture thread started.\n\n"
    );

    auto statsTime =
        std::chrono::steady_clock::now();

    uint64_t statsFrameCount = 0;

    while (
        m_running.load(
            std::memory_order_acquire
        )
        )
    {
        const auto frameStart =
            std::chrono::steady_clock::now();

        if (CaptureFrame())
        {
            m_framesCaptured++;

            // -------------------------------------------------
            // Capture frame time
            // -------------------------------------------------

            const auto frameEnd =
                std::chrono::steady_clock::now();

            const double frameTimeMs =
                std::chrono::duration<double, std::milli>(
                    frameEnd - frameStart
                ).count();

            m_captureFrameTimeMs =
                frameTimeMs;

            // -------------------------------------------------
            // Capture FPS
            // -------------------------------------------------

            if (
                statsTime.time_since_epoch().count() == 0
                )
            {
                statsTime =
                    frameEnd;

                statsFrameCount =
                    m_framesCaptured.load(
                        std::memory_order_relaxed
                    );
            }

            const double elapsed =
                std::chrono::duration<double>(
                    frameEnd - statsTime
                ).count();

            if (elapsed >= 1.0)
            {
                const uint64_t totalFrames =
                    m_framesCaptured.load(
                        std::memory_order_relaxed
                    );

                const uint64_t framesSinceLastUpdate =
                    totalFrames -
                    statsFrameCount;

                m_captureFps =
                    static_cast<double>(
                        framesSinceLastUpdate
                        ) /
                    elapsed;

                statsFrameCount =
                    totalFrames;

                statsTime =
                    frameEnd;
            }
        }
        else
        {
            std::this_thread::yield();
        }
    }

    Logger::Log(
        "[Camera] Capture thread stopped.\n\n"
    );
}


// =========================================================
// CaptureFrame
// =========================================================

bool Camera::CaptureFrame()
{
    if (!m_reader)
        return false;

    // -----------------------------------------------------
    // Read sample
    // -----------------------------------------------------

    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG timestamp = 0;

    Microsoft::WRL::ComPtr<IMFSample> sample;

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
            "[Camera] ReadSample failed. HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        {
            std::lock_guard<std::mutex> lock(m_errorMutex);

            m_lastError = "Camera disconnected or stopped responding!";
        }

        m_runtimeFailure = true;
        m_running = false;

        return false;
    }

    if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
    {
        Logger::Log(
            "[Camera] End of stream.\n"
        );

        return false;
    }

    if (flags & MF_SOURCE_READERF_STREAMTICK)
    {
        return false;
    }

    if (!sample)
        return false;

    // -----------------------------------------------------
    // Get media buffer
    // -----------------------------------------------------

    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;

    hr =
        sample->ConvertToContiguousBuffer(
            &buffer
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] ConvertToContiguousBuffer failed. "
            "HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

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
        Logger::Log(
            "[Camera] Buffer Lock failed. HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        return false;
    }

    bool success = false;

    // =====================================================
    // NV12
    // =====================================================

    if (m_format == CameraFormat::NV12)
    {
        const size_t expectedSize =
            GetExpectedNV12FrameSize();

        if (
            static_cast<size_t>(currentLength) <
            expectedSize
            )
        {
            Logger::Log(
                "[Camera] NV12 sample too small. "
                "Expected=%zu Actual=%u\n",
                expectedSize,
                currentLength
            );

            buffer->Unlock();

            return false;
        }

        // -------------------------------------------------
        // Copy raw NV12 frame.
        //
        // Layout:
        //
        //     Y plane
        //     width * height
        //
        //     UV plane
        //     width * height / 2
        // -------------------------------------------------

        std::memcpy(
            m_captureBuffer.data(),
            data,
            expectedSize
        );

        m_captureFrameBytes =
            expectedSize;

        success = true;
    }

    // =====================================================
    // MJPG
    // =====================================================

    else if (m_format == CameraFormat::MJPG)
    {
        // -------------------------------------------------
        // WIC stream
        // -------------------------------------------------

        Microsoft::WRL::ComPtr<IWICStream> stream;

        hr =
            m_wicFactory->CreateStream(
                &stream
            );

        if (FAILED(hr))
        {
            Logger::Log(
                "[Camera] WIC CreateStream failed.\n"
            );

            buffer->Unlock();

            return false;
        }

        hr =
            stream->InitializeFromMemory(
                data,
                currentLength
            );

        if (FAILED(hr))
        {
            Logger::Log(
                "[Camera] WIC InitializeFromMemory failed.\n"
            );

            buffer->Unlock();

            return false;
        }

        // -------------------------------------------------
        // JPEG decoder
        // -------------------------------------------------

        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;

        hr =
            m_wicFactory->CreateDecoderFromStream(
                stream.Get(),
                nullptr,
                WICDecodeMetadataCacheOnDemand,
                &decoder
            );

        if (FAILED(hr))
        {
            Logger::Log(
                "[Camera] WIC JPEG decoder creation failed.\n"
            );

            buffer->Unlock();

            return false;
        }

        // -------------------------------------------------
        // Get frame
        // -------------------------------------------------

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;

        hr =
            decoder->GetFrame(
                0,
                &frame
            );

        if (FAILED(hr))
        {
            Logger::Log(
                "[Camera] WIC GetFrame failed.\n"
            );

            buffer->Unlock();

            return false;
        }

        // -------------------------------------------------
        // Format converter
        // -------------------------------------------------

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;

        hr =
            m_wicFactory->CreateFormatConverter(
                &converter
            );

        if (FAILED(hr))
        {
            Logger::Log(
                "[Camera] WIC CreateFormatConverter failed.\n"
            );

            buffer->Unlock();

            return false;
        }

        hr =
            converter->Initialize(
                frame.Get(),
                GUID_WICPixelFormat32bppBGRA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0,
                WICBitmapPaletteTypeCustom
            );

        if (FAILED(hr))
        {
            Logger::Log(
                "[Camera] WIC converter initialization failed.\n"
            );

            buffer->Unlock();

            return false;
        }

        // -------------------------------------------------
        // Copy decoded pixels
        // -------------------------------------------------

        const size_t rowStride =
            static_cast<size_t>(m_width) * 4;

        const size_t bufferSize =
            GetExpectedBGRASize();

        hr =
            converter->CopyPixels(
                nullptr,
                static_cast<UINT>(rowStride),
                static_cast<UINT>(bufferSize),
                m_captureBuffer.data()
            );

        if (FAILED(hr))
        {
            Logger::Log(
                "[Camera] WIC CopyPixels failed. HRESULT: 0x" +
                std::to_string(
                    static_cast<unsigned long>(hr)
                ) +
                "\n"
            );

            buffer->Unlock();

            return false;
        }

        m_captureFrameBytes =
            bufferSize;

        success = true;
    }

    buffer->Unlock();

    if (!success)
        return false;

    // -----------------------------------------------------
    // Record frame
    // -----------------------------------------------------

    // -----------------------------------------------------
    // Publish frame
    // -----------------------------------------------------

    {
        std::lock_guard<std::mutex> lock(
            m_frameMutex
        );

        if (m_recording.load(std::memory_order_acquire))
        {
            const double recordingTime =
                m_recordingTime.load(
                    std::memory_order_relaxed
                );

            const int64_t timestamp100ns =
                m_recordingTime * 10'000'000.0;

            m_videoEncoder.EncodeFrame(
                m_captureBuffer.data(),
                m_captureFrameBytes,
                timestamp100ns
            );
        }

        if (
            m_newFrameAvailable.load(
                std::memory_order_relaxed
            )
            )
        {
            m_framesDropped++;
        }

        std::swap(
            m_captureBuffer,
            m_readyBuffer
        );

        m_newFrameAvailable.store(
            true,
            std::memory_order_release
        );
    }

    ClearLastError();

    return true;
}

std::string Camera::GetLastError() const
{
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_lastError;
}

void Camera::ClearLastError()
{
    std::lock_guard<std::mutex> lock(m_errorMutex);
    m_lastError.clear();
}


// =========================================================
// Update
// =========================================================

bool Camera::Update(double time)
{
    if (m_runtimeFailure)
    {
        Logger::Log(
            "[Camera] Camera disconnected or stopped responding.\n"
        );

        CloseCamera();

        EnumerateCameras();

        m_runtimeFailure = false;
        

        return false;
    }

    if (!m_open)
        return false;

    if (
        !m_newFrameAvailable.load(
            std::memory_order_acquire
        )
        )
    {
        return false;
    }

    m_recordingTime.store(
        time,
        std::memory_order_relaxed
    );

    // -----------------------------------------------------
    // Get latest frame
    // -----------------------------------------------------

    {
        std::lock_guard<std::mutex> lock(
            m_frameMutex
        );

        if (m_readyBuffer.empty())
        {
            m_newFrameAvailable.store(
                false,
                std::memory_order_release
            );

            return false;
        }

        std::swap(
            m_readyBuffer,
            m_uploadBuffer
        );

        m_newFrameAvailable.store(
            false,
            std::memory_order_release
        );
    }

    // -----------------------------------------------------
    // Measure upload time
    // -----------------------------------------------------

    const auto uploadStart =
        std::chrono::steady_clock::now();

    const bool success =
        UploadFrame(
            m_uploadBuffer.data(),
            m_uploadBuffer.size()
        );

    const auto uploadEnd =
        std::chrono::steady_clock::now();

    if (!success)
        return false;

    // -----------------------------------------------------
    // Upload frame time
    // -----------------------------------------------------

    const double uploadFrameTimeMs =
        std::chrono::duration<double, std::milli>(
            uploadEnd - uploadStart
        ).count();

    m_uploadFrameTimeMs =
        uploadFrameTimeMs;

    // -----------------------------------------------------
    // Count uploaded frame
    // -----------------------------------------------------

    m_framesUploaded++;

    // -----------------------------------------------------
    // Initialize upload statistics
    // -----------------------------------------------------

    if (
        m_uploadStatsTime.time_since_epoch().count() == 0
        )
    {
        m_uploadStatsTime =
            uploadEnd;

        m_uploadStatsFrameCount =
            m_framesUploaded.load(
                std::memory_order_relaxed
            );
    }

    // -----------------------------------------------------
    // Calculate upload FPS
    // -----------------------------------------------------

    const double elapsed =
        std::chrono::duration<double>(
            uploadEnd - m_uploadStatsTime
        ).count();

    if (elapsed >= 1.0)
    {
        const uint64_t totalFrames =
            m_framesUploaded.load(
                std::memory_order_relaxed
            );

        const uint64_t framesSinceLastUpdate =
            totalFrames -
            m_uploadStatsFrameCount;

        m_uploadFps =
            static_cast<double>(
                framesSinceLastUpdate
                ) /
            elapsed;

        m_uploadStatsFrameCount =
            totalFrames;

        m_uploadStatsTime =
            uploadEnd;
    }

    return true;
}


// =========================================================
// Recording
// =========================================================

bool Camera::StartRecording(
    const std::string& filePath
)
{
    std::lock_guard<std::mutex> lock(
        m_recordingMutex
    );

    if (m_recording)
    {
        return false;
    }

    const int width =
        GetWidth();

    const int height =
        GetHeight();

    const int fps =
        30;

    CameraVideoEncoder::PixelFormat pixelFormat;

    if (m_format == CameraFormat::NV12)
    {
        pixelFormat =
            CameraVideoEncoder::PixelFormat::NV12;
    }
    else if (m_format == CameraFormat::MJPG)
    {
        // MJPG is decoded into BGRA/RGB32 in
        // the capture code before being passed
        // to the encoder.
        pixelFormat =
            CameraVideoEncoder::PixelFormat::RGB32;
    }
    else
    {
        Logger::Log(
            "[Recording] Unsupported recording format.\n"
        );

        return false;
    }

    if (!m_videoEncoder.Start(
        filePath,
        width,
        height,
        fps,
        pixelFormat
    ))
    {
        Logger::Log(
            "[Recording] Failed to start video encoder.\n"
        );

        return false;
    }

    m_recordingStartTime =
        std::chrono::steady_clock::now();

    m_recording.store(
        true,
        std::memory_order_release
    );

    return true;
}

bool Camera::StopRecording()
{
    std::lock_guard<std::mutex> lock(
        m_recordingMutex
    );

    if (!m_recording)
    {
        return false;
    }

    m_recording.store(
        false,
        std::memory_order_release
    );

    m_videoEncoder.Stop();

    Logger::Log(
        "[Recording] Recording stopped.\n"
    );

    return true;
}

bool Camera::IsRecording() const
{
    return m_recording.load(
        std::memory_order_acquire
    );
}

// =========================================================
// UploadFrame
// =========================================================

bool Camera::UploadFrame(
    const BYTE* pixels,
    size_t pixelSize
)
{
    if (
        !pixels ||
        pixelSize == 0 ||
        !m_texture ||
        !m_context
        )
    {
        LOG_UPLOAD(
            "INVALID INPUT"
        );

        return false;
    }

    if (m_format == CameraFormat::NV12)
    {
        return UploadNV12Frame(
            pixels,
            pixelSize
        );
    }

    if (m_format == CameraFormat::MJPG)
    {
        return UploadMJPGFrame(
            pixels,
            pixelSize
        );
    }

    return false;
}


// =========================================================
// UploadNV12Frame
// =========================================================

bool Camera::UploadNV12Frame(
    const BYTE* data,
    size_t dataSize
)
{
    if (
        !data ||
        !m_nv12YTexture ||
        !m_nv12UVTexture ||
        !m_context
        )
    {
        return false;
    }

    const size_t expectedSize =
        GetExpectedNV12FrameSize();

    if (dataSize < expectedSize)
    {
        Logger::Log(
            "[Camera] NV12 upload buffer too small.\n"
        );

        return false;
    }

    // -----------------------------------------------------
    // Y plane
    // -----------------------------------------------------

    const BYTE* yData =
        data;

    D3D11_BOX yBox{};

    yBox.left = 0;
    yBox.top = 0;
    yBox.front = 0;

    yBox.right =
        static_cast<UINT>(m_width);

    yBox.bottom =
        static_cast<UINT>(m_height);

    yBox.back = 1;

    const UINT yRowPitch =
        static_cast<UINT>(m_width);

    m_context->UpdateSubresource(
        m_nv12YTexture.Get(),
        0,
        &yBox,
        yData,
        yRowPitch,
        0
    );

    // -----------------------------------------------------
    // UV plane
    // -----------------------------------------------------

    const BYTE* uvData =
        data +
        m_nv12YSize;

    D3D11_BOX uvBox{};

    uvBox.left = 0;
    uvBox.top = 0;
    uvBox.front = 0;

    uvBox.right =
        static_cast<UINT>(m_width / 2);

    uvBox.bottom =
        static_cast<UINT>(m_height / 2);

    uvBox.back = 1;

    const UINT uvRowPitch =
        static_cast<UINT>(m_width);

    m_context->UpdateSubresource(
        m_nv12UVTexture.Get(),
        0,
        &uvBox,
        uvData,
        uvRowPitch,
        0
    );

    // -----------------------------------------------------
    // Convert to final BGRA texture
    // -----------------------------------------------------

    return ConvertNV12ToBGRA();
}


// =========================================================
// UploadMJPGFrame
// =========================================================

bool Camera::UploadMJPGFrame(
    const BYTE* pixels,
    size_t pixelSize
)
{
    if (
        !pixels ||
        !m_texture ||
        !m_context
        )
    {
        return false;
    }

    const size_t expectedSize =
        GetExpectedBGRASize();

    if (pixelSize < expectedSize)
    {
        Logger::Log(
            "[Camera] MJPG BGRA buffer too small.\n"
        );

        return false;
    }

    const UINT rowPitch =
        static_cast<UINT>(
            static_cast<size_t>(m_width) * 4
            );

    m_context->UpdateSubresource(
        m_texture.Get(),
        0,
        nullptr,
        pixels,
        rowPitch,
        0
    );

    return true;
}


// =========================================================
// ConvertNV12ToBGRA
// =========================================================

bool Camera::ConvertNV12ToBGRA()
{
    if (
        !m_context ||
        !m_renderTargetView ||
        !m_nv12YSRV ||
        !m_nv12UVSRV ||
        !m_nv12VertexShader ||
        !m_nv12PixelShader ||
        !m_nv12Sampler
        )
    {
        return false;
    }

    // -----------------------------------------------------
    // Save render target state
    // -----------------------------------------------------

    ID3D11RenderTargetView* oldRTV = nullptr;
    ID3D11DepthStencilView* oldDSV = nullptr;

    m_context->OMGetRenderTargets(
        1,
        &oldRTV,
        &oldDSV
    );

    // -----------------------------------------------------
    // Save viewport
    // -----------------------------------------------------

    UINT oldViewportCount = 1;

    D3D11_VIEWPORT oldViewport{};

    m_context->RSGetViewports(
        &oldViewportCount,
        &oldViewport
    );

    // -----------------------------------------------------
    // Save shaders
    // -----------------------------------------------------

    ID3D11VertexShader* oldVS = nullptr;
    ID3D11PixelShader* oldPS = nullptr;

    m_context->VSGetShader(
        &oldVS,
        nullptr,
        nullptr
    );

    m_context->PSGetShader(
        &oldPS,
        nullptr,
        nullptr
    );

    // -----------------------------------------------------
    // Save pixel shader resources
    // -----------------------------------------------------

    ID3D11ShaderResourceView* oldSRVs[2] =
    {
        nullptr,
        nullptr
    };

    m_context->PSGetShaderResources(
        0,
        2,
        oldSRVs
    );

    // -----------------------------------------------------
    // Save sampler
    // -----------------------------------------------------

    ID3D11SamplerState* oldSampler = nullptr;

    m_context->PSGetSamplers(
        0,
        1,
        &oldSampler
    );

    // -----------------------------------------------------
    // Set camera render target
    // -----------------------------------------------------

    ID3D11RenderTargetView* renderTarget =
        m_renderTargetView.Get();

    m_context->OMSetRenderTargets(
        1,
        &renderTarget,
        nullptr
    );

    // -----------------------------------------------------
    // Camera viewport
    // -----------------------------------------------------

    D3D11_VIEWPORT viewport{};

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    viewport.Width =
        static_cast<float>(m_width);

    viewport.Height =
        static_cast<float>(m_height);

    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    m_context->RSSetViewports(
        1,
        &viewport
    );

    // -----------------------------------------------------
    // Set shaders
    // -----------------------------------------------------

    m_context->VSSetShader(
        m_nv12VertexShader.Get(),
        nullptr,
        0
    );

    m_context->PSSetShader(
        m_nv12PixelShader.Get(),
        nullptr,
        0
    );

    // -----------------------------------------------------
    // Set NV12 textures
    // -----------------------------------------------------

    ID3D11ShaderResourceView* srvs[2] =
    {
        m_nv12YSRV.Get(),
        m_nv12UVSRV.Get()
    };

    m_context->PSSetShaderResources(
        0,
        2,
        srvs
    );

    // -----------------------------------------------------
    // Set sampler
    // -----------------------------------------------------

    ID3D11SamplerState* sampler =
        m_nv12Sampler.Get();

    m_context->PSSetSamplers(
        0,
        1,
        &sampler
    );

    // -----------------------------------------------------
    // Draw fullscreen triangle
    // -----------------------------------------------------

    m_context->IASetInputLayout(
        nullptr
    );

    m_context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
    );

    m_context->Draw(
        3,
        0
    );

    // -----------------------------------------------------
    // IMPORTANT:
    //
    // Unbind camera SRVs before restoring the old state.
    //
    // Otherwise the final BGRA render target can remain
    // simultaneously bound as an input/output resource.
    // -----------------------------------------------------

    ID3D11ShaderResourceView* nullSRVs[2] =
    {
        nullptr,
        nullptr
    };

    m_context->PSSetShaderResources(
        0,
        2,
        nullSRVs
    );

    // -----------------------------------------------------
    // Restore shaders
    // -----------------------------------------------------

    m_context->VSSetShader(
        oldVS,
        nullptr,
        0
    );

    m_context->PSSetShader(
        oldPS,
        nullptr,
        0
    );

    // -----------------------------------------------------
    // Restore SRVs
    // -----------------------------------------------------

    m_context->PSSetShaderResources(
        0,
        2,
        oldSRVs
    );

    // -----------------------------------------------------
    // Restore sampler
    // -----------------------------------------------------

    m_context->PSSetSamplers(
        0,
        1,
        &oldSampler
    );

    // -----------------------------------------------------
    // Restore render targets
    // -----------------------------------------------------

    m_context->OMSetRenderTargets(
        1,
        &oldRTV,
        oldDSV
    );

    // -----------------------------------------------------
    // Restore viewport
    // -----------------------------------------------------

    if (oldViewportCount > 0)
    {
        m_context->RSSetViewports(
            oldViewportCount,
            &oldViewport
        );
    }

    // -----------------------------------------------------
    // Release COM references returned by Get calls
    // -----------------------------------------------------

    if (oldRTV)
        oldRTV->Release();

    if (oldDSV)
        oldDSV->Release();

    if (oldVS)
        oldVS->Release();

    if (oldPS)
        oldPS->Release();

    for (auto& srv : oldSRVs)
    {
        if (srv)
            srv->Release();
    }

    if (oldSampler)
        oldSampler->Release();

    return true;
}



// =========================================================
// EnumerateCameras
// =========================================================

bool Camera::EnumerateCameras()
{
    m_availableCameras.clear();

    Microsoft::WRL::ComPtr<IMFAttributes>
        attributes;

    HRESULT hr =
        MFCreateAttributes(
            &attributes,
            1
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] MFCreateAttributes failed. HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        return false;
    }

    hr =
        attributes->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] Failed to set video capture attribute.\n"
        );

        return false;
    }

    IMFActivate** devices = nullptr;
    UINT32 count = 0;

    hr =
        MFEnumDeviceSources(
            attributes.Get(),
            &devices,
            &count
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[Camera] MFEnumDeviceSources failed. HRESULT: 0x" +
            std::to_string(
                static_cast<unsigned long>(hr)
            ) +
            "\n"
        );

        return false;
    }

    Logger::Log(
        "[Camera] Found %u camera(s).\n",
        count
    );

    for (UINT32 i = 0; i < count; ++i)
    {
        WCHAR* friendlyName = nullptr;
        UINT32 nameLength = 0;

        std::string name =
            "Camera " +
            std::to_string(i);

        if (
            devices[i] &&
            SUCCEEDED(
                devices[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                    &friendlyName,
                    &nameLength
                )
            )
            )
        {
            int length =
                WideCharToMultiByte(
                    CP_UTF8,
                    0,
                    friendlyName,
                    static_cast<int>(nameLength),
                    nullptr,
                    0,
                    nullptr,
                    nullptr
                );

            if (length > 0)
            {
                std::string converted(
                    length,
                    '\0'
                );

                WideCharToMultiByte(
                    CP_UTF8,
                    0,
                    friendlyName,
                    static_cast<int>(nameLength),
                    converted.data(),
                    length,
                    nullptr,
                    nullptr
                );

                name = converted;
            }

            CoTaskMemFree(
                friendlyName
            );
        }

        CameraDevice device{};

        device.name = name;
        device.index = static_cast<int>(i);

        m_availableCameras.push_back(
            device
        );

        Logger::Log(
            "[Camera] Camera %d: %s\n",
            device.index,
            device.name.c_str()
        );
    }

    for (UINT32 i = 0; i < count; ++i)
    {
        if (devices[i])
            devices[i]->Release();
    }

    CoTaskMemFree(devices);

    return true;
}


const std::vector<Camera::CameraDevice>& Camera::GetAvailableCameras() const
{
    return m_availableCameras;
}


int Camera::GetCameraIndex() const
{
    return m_cameraIndex;
}


// =========================================================
// OpenCamera
// =========================================================

bool Camera::OpenCamera(
    int cameraIndex
)
{
    ClearLastError();

    if (
        cameraIndex < 0 ||
        cameraIndex >= static_cast<int>(m_availableCameras.size())
        )
    {
        Logger::Log("[Camera] Invalid camera index: %d\n", cameraIndex);

        {
            std::lock_guard<std::mutex> lock(m_errorMutex);

            m_lastError = "(Invalid camera index:" +
                std::to_string(cameraIndex) +
                ")";
        }

        return false;
    }

    const std::string cameraName =
        m_availableCameras[cameraIndex].name;

    // Close currently open camera first.

    if (IsOpen())
    {
        CloseCamera();
    }

    m_cameraIndex =
        cameraIndex;

    IMFActivate* camera =
        nullptr;

    if (!FindCamera(
        cameraIndex,
        &camera
    ))
    {
        Logger::Log(
            "[Camera] Failed to find camera index %d.\n",
            cameraIndex
        );
        
        {
            std::lock_guard<std::mutex> lock(m_errorMutex);

            m_lastError = 
                cameraName +
                "\"\n" +
                "(Failed to find camera index:" +
                std::to_string(cameraIndex) +
                ")";
        }

        return false;
    }

    bool readerCreated =
        CreateReader(
            camera,
            m_requestedFormat
        );

    if (
        !readerCreated &&
        m_requestedFormat ==
        CameraFormat::NV12
        )
    {
        Logger::Log(
            "[Camera] NV12 unavailable. Falling back to MJPG.\n"
        );

        readerCreated =
            CreateReader(
                camera,
                CameraFormat::MJPG
            );
    }

    camera->Release();

    if (!readerCreated)
    {
        {
            std::lock_guard<std::mutex> lock(m_errorMutex);

            m_lastError = 
                cameraName +
                "\"\n" + 
                "(Failed to create reader)";
        }

        Logger::Log(
            "[Camera] Failed to open camera \"%s\".\n",
            m_availableCameras[cameraIndex].name.c_str()
        );

        return false;
    }

    if (!CreateTexture())
    {
        {
            std::lock_guard<std::mutex> lock(m_errorMutex);

            m_lastError = 
                cameraName +
                "\"\n" +
                "(Failed to create texture)";
        }

        Logger::Log(
            "[Camera] Failed to create camera texture for \"%s\".\n",
            cameraName.c_str()
        );

        m_reader.Reset();
        return false;
    }

    try
    {
        m_captureBuffer.resize(
            m_frameBufferSize
        );

        m_readyBuffer.resize(
            m_frameBufferSize
        );

        m_uploadBuffer.resize(
            m_frameBufferSize
        );
    }
    catch (...)
    {
        Logger::Log(
            "[Camera] Failed to allocate frame buffers.\n"
        );

        CloseCamera();

        {
            std::lock_guard<std::mutex> lock(m_errorMutex);

            m_lastError = 
                cameraName +
                "\"\n" +
                "(Failed to allocate frame buffers.)";
        }

        return false;
    }

    m_newFrameAvailable = false;

    m_framesCaptured = 0;
    m_framesUploaded = 0;
    m_framesDropped = 0;

    m_captureFrameBytes = 0;

    m_captureFps = 0.0;
    m_captureFrameTimeMs = 0.0;

    m_uploadFps = 0.0;
    m_uploadFrameTimeMs = 0.0;

    m_uploadStatsTime =
        std::chrono::steady_clock::time_point{};

    m_uploadStatsFrameCount = 0;

    m_runtimeFailure = false;
    m_open = true;
    m_running = true;

    m_cameraThread =
        std::thread(
            &Camera::CameraThread,
            this
        );

    Logger::Log(
        "[Camera] Camera %d opened: %s\n",
        cameraIndex,
        m_availableCameras[
            cameraIndex
        ].name.c_str()
                );

    return true;
}


// =========================================================
// CloseCamera
// =========================================================

void Camera::CloseCamera()
{
    m_open.store(
        false,
        std::memory_order_release
    );

    m_running.store(
        false,
        std::memory_order_release
    );

    if (m_cameraThread.joinable())
    {
        m_cameraThread.join();
    }

    ReleaseNV12Resources();

    m_renderTargetView.Reset();
    m_shaderResourceView.Reset();
    m_texture.Reset();

    m_reader.Reset();

    m_captureBuffer.clear();
    m_readyBuffer.clear();
    m_uploadBuffer.clear();

    m_newFrameAvailable.store(
        false,
        std::memory_order_release
    );

    m_availableModes.clear();

    m_width = 0;
    m_height = 0;

    m_fpsNumerator = 0;
    m_fpsDenominator = 1;

    m_format =
        CameraFormat::None;

    m_frameBufferSize = 0;

    m_nv12YSize = 0;
    m_nv12UVSize = 0;

    m_controls.Shutdown();

    m_cameraIndex = -1;
}


// =========================================================
// SetFormat
// =========================================================

bool Camera::SetFormat(
    CameraFormat format
)
{
    if (
        format != CameraFormat::NV12 &&
        format != CameraFormat::MJPG
        )
    {
        return false;
    }

    if (!m_open)
        return false;

    if (format == m_format)
        return true;

    const CameraFormat previousFormat =
        m_format;

    const CameraMode previousMode =
        m_requestedMode;

    Logger::Log(
        "[Camera] Switching format: %s -> %s\n",
        previousFormat == CameraFormat::NV12
        ? "NV12"
        : "MJPG",
        format == CameraFormat::NV12
        ? "NV12"
        : "MJPG"
    );

    // -----------------------------------------------------
    // Stop camera thread
    // -----------------------------------------------------

    m_running.store(
        false,
        std::memory_order_release
    );

    if (m_cameraThread.joinable())
    {
        m_cameraThread.join();
    }

    // -----------------------------------------------------
    // Release old reader
    // -----------------------------------------------------

    m_reader.Reset();

    // -----------------------------------------------------
    // Release old GPU resources
    // -----------------------------------------------------

    ReleaseNV12Resources();

    m_renderTargetView.Reset();
    m_shaderResourceView.Reset();
    m_texture.Reset();

    // -----------------------------------------------------
    // Clear requested mode.
    //
    // We intentionally do NOT try to preserve the old
    // resolution/FPS when changing format.
    //
    // CreateReader() will therefore choose the best
    // available mode for the new format.
    // -----------------------------------------------------

    m_requestedMode =
        CameraMode{};

    // -----------------------------------------------------
    // Find camera again
    // -----------------------------------------------------

    IMFActivate* camera = nullptr;

    if (
        !FindCamera(
            m_cameraIndex,
            &camera
        )
        )
    {
        Logger::Log(
            "[Camera] Failed to find camera while "
            "switching format.\n"
        );

        // Restore requested mode state.
        m_requestedMode =
            previousMode;

        m_open.store(
            false,
            std::memory_order_release
        );

        return false;
    }

    // -----------------------------------------------------
    // Create requested format
    // -----------------------------------------------------

    if (
        !CreateReader(
            camera,
            format
        )
        )
    {
        camera->Release();

        Logger::Log(
            "[Camera] Requested format is not available.\n"
        );

        // -------------------------------------------------
        // Restore previous format + mode
        // -------------------------------------------------

        m_requestedMode =
            previousMode;

        IMFActivate* restoreCamera = nullptr;

        if (
            FindCamera(
                m_cameraIndex,
                &restoreCamera
            )
            )
        {
            if (
                CreateReader(
                    restoreCamera,
                    previousFormat
                ) &&
                CreateTexture()
                )
            {
                restoreCamera->Release();

                try
                {
                    m_captureBuffer.resize(
                        m_frameBufferSize
                    );

                    m_readyBuffer.resize(
                        m_frameBufferSize
                    );

                    m_uploadBuffer.resize(
                        m_frameBufferSize
                    );
                }
                catch (...)
                {
                    Logger::Log(
                        "[Camera] Failed to restore "
                        "frame buffers.\n"
                    );

                    Shutdown();

                    return false;
                }

                // -----------------------------------------
                // Reset frame state
                // -----------------------------------------

                {
                    std::lock_guard<std::mutex> lock(
                        m_frameMutex
                    );

                    m_newFrameAvailable.store(
                        false,
                        std::memory_order_release
                    );
                }

                // -----------------------------------------
                // Reset statistics
                // -----------------------------------------

                m_framesCaptured = 0;
                m_framesUploaded = 0;
                m_framesDropped = 0;

                m_captureFrameBytes = 0;

                m_captureFps = 0.0;
                m_captureFrameTimeMs = 0.0;

                m_uploadFps = 0.0;
                m_uploadFrameTimeMs = 0.0;

                m_uploadStatsTime =
                    std::chrono::steady_clock::time_point{};

                m_uploadStatsFrameCount = 0;

                // -----------------------------------------
                // Restart camera
                // -----------------------------------------

                m_open.store(
                    true,
                    std::memory_order_release
                );

                m_running.store(
                    true,
                    std::memory_order_release
                );

                m_cameraThread =
                    std::thread(
                        &Camera::CameraThread,
                        this
                    );

                Logger::Log(
                    "[Camera] Restored previous "
                    "camera format.\n"
                );

                return false;
            }

            restoreCamera->Release();
        }

        Logger::Log(
            "[Camera] Failed to restore previous "
            "camera format.\n"
        );

        m_open.store(
            false,
            std::memory_order_release
        );

        return false;
    }

    camera->Release();

    // -----------------------------------------------------
    // Recreate GPU resources
    // -----------------------------------------------------

    if (!CreateTexture())
    {
        Logger::Log(
            "[Camera] Failed to recreate camera texture "
            "after format switch.\n"
        );

        m_reader.Reset();

        m_requestedMode =
            previousMode;

        m_open.store(
            false,
            std::memory_order_release
        );

        return false;
    }

    // -----------------------------------------------------
    // Resize CPU buffers
    // -----------------------------------------------------

    try
    {
        m_captureBuffer.resize(
            m_frameBufferSize
        );

        m_readyBuffer.resize(
            m_frameBufferSize
        );

        m_uploadBuffer.resize(
            m_frameBufferSize
        );
    }
    catch (...)
    {
        Logger::Log(
            "[Camera] Failed to resize camera buffers "
            "after format switch.\n"
        );

        Shutdown();

        return false;
    }

    // -----------------------------------------------------
    // Reset frame state
    // -----------------------------------------------------

    {
        std::lock_guard<std::mutex> lock(
            m_frameMutex
        );

        m_newFrameAvailable.store(
            false,
            std::memory_order_release
        );
    }

    // -----------------------------------------------------
    // Reset statistics
    // -----------------------------------------------------

    m_framesCaptured = 0;
    m_framesUploaded = 0;
    m_framesDropped = 0;

    m_captureFrameBytes = 0;

    m_captureFps = 0.0;
    m_captureFrameTimeMs = 0.0;

    m_uploadFps = 0.0;
    m_uploadFrameTimeMs = 0.0;

    m_uploadStatsTime =
        std::chrono::steady_clock::time_point{};

    m_uploadStatsFrameCount = 0;

    // -----------------------------------------------------
    // Store requested format
    // -----------------------------------------------------

    m_requestedFormat =
        format;

    // -----------------------------------------------------
    // CreateReader() already stored the actual selected
    // mode in m_requestedMode.
    // -----------------------------------------------------

    // -----------------------------------------------------
    // Restart camera thread
    // -----------------------------------------------------

    m_open.store(
        true,
        std::memory_order_release
    );

    m_running.store(
        true,
        std::memory_order_release
    );

    m_cameraThread =
        std::thread(
            &Camera::CameraThread,
            this
        );

    Logger::Log(
        "[Camera] Format switched successfully to %s.\n",
        m_format == CameraFormat::NV12
        ? "NV12"
        : "MJPG"
    );

    Logger::Log(
        "[Camera] Active mode: %dx%d @ %u/%u FPS\n",
        m_width,
        m_height,
        m_fpsNumerator,
        m_fpsDenominator
    );

    return true;
}


// =========================================================
// GetFormat
// =========================================================

Camera::CameraFormat Camera::GetFormat() const
{
    return m_format;
}

// =========================================================
// Camera Settings
// =========================================================

const std::vector<Camera::CameraMode>&
Camera::GetAvailableModes() const
{
    return m_availableModes;
}

// =========================================================
// SetMode
// =========================================================

bool Camera::SetMode(
    int width,
    int height,
    UINT32 fpsNumerator,
    UINT32 fpsDenominator
)
{
    if (!m_open)
        return false;

    if (
        width <= 0 ||
        height <= 0 ||
        fpsNumerator == 0 ||
        fpsDenominator == 0
        )
    {
        return false;
    }

    // -----------------------------------------------------
    // Check that requested mode actually exists
    // -----------------------------------------------------

    bool modeExists = false;

    for (const CameraMode& mode :
        m_availableModes)
    {
        if (
            mode.width == width &&
            mode.height == height &&
            mode.fpsNumerator == fpsNumerator &&
            mode.fpsDenominator == fpsDenominator
            )
        {
            modeExists = true;
            break;
        }
    }

    if (!modeExists)
    {
        Logger::Log(
            "[Camera] Requested mode is not available: "
            "%dx%d @ %u/%u FPS\n",
            width,
            height,
            fpsNumerator,
            fpsDenominator
        );

        return false;
    }

    // -----------------------------------------------------
    // Already using this mode
    // -----------------------------------------------------

    if (
        m_width == width &&
        m_height == height &&
        m_fpsNumerator == fpsNumerator &&
        m_fpsDenominator == fpsDenominator
        )
    {
        return true;
    }

    // -----------------------------------------------------
    // Save current mode for rollback
    // -----------------------------------------------------

    const CameraMode previousMode =
        m_requestedMode;

    const int previousWidth =
        m_width;

    const int previousHeight =
        m_height;

    const UINT32 previousFpsNumerator =
        m_fpsNumerator;

    const UINT32 previousFpsDenominator =
        m_fpsDenominator;

    Logger::Log(
        "[Camera] Switching mode: "
        "%dx%d @ %u/%u -> "
        "%dx%d @ %u/%u FPS\n",
        previousWidth,
        previousHeight,
        previousFpsNumerator,
        previousFpsDenominator,
        width,
        height,
        fpsNumerator,
        fpsDenominator
    );

    // -----------------------------------------------------
    // Stop camera thread
    // -----------------------------------------------------

    m_running.store(
        false,
        std::memory_order_release
    );

    if (m_cameraThread.joinable())
    {
        m_cameraThread.join();
    }

    // -----------------------------------------------------
    // Release old reader
    // -----------------------------------------------------

    m_reader.Reset();

    // -----------------------------------------------------
    // Release old GPU resources
    // -----------------------------------------------------

    ReleaseNV12Resources();

    m_renderTargetView.Reset();
    m_shaderResourceView.Reset();
    m_texture.Reset();

    // -----------------------------------------------------
    // Store requested mode
    // -----------------------------------------------------

    m_requestedMode.width =
        width;

    m_requestedMode.height =
        height;

    m_requestedMode.fpsNumerator =
        fpsNumerator;

    m_requestedMode.fpsDenominator =
        fpsDenominator;

    // -----------------------------------------------------
    // Find camera
    // -----------------------------------------------------

    IMFActivate* camera = nullptr;

    if (
        !FindCamera(
            m_cameraIndex,
            &camera
        )
        )
    {
        Logger::Log(
            "[Camera] Failed to find camera while "
            "switching mode.\n"
        );

        // Restore requested state.
        m_requestedMode =
            previousMode;

        m_open.store(
            false,
            std::memory_order_release
        );

        return false;
    }

    // -----------------------------------------------------
    // Create requested mode
    // -----------------------------------------------------

    if (
        !CreateReader(
            camera,
            m_format
        )
        )
    {
        camera->Release();

        Logger::Log(
            "[Camera] Failed to create requested "
            "camera mode.\n"
        );

        // -------------------------------------------------
        // Restore previous mode
        // -------------------------------------------------

        m_requestedMode =
            previousMode;

        IMFActivate* restoreCamera = nullptr;

        if (
            FindCamera(
                m_cameraIndex,
                &restoreCamera
            )
            )
        {
            if (
                CreateReader(
                    restoreCamera,
                    m_format
                ) &&
                CreateTexture()
                )
            {
                restoreCamera->Release();

                try
                {
                    m_captureBuffer.resize(
                        m_frameBufferSize
                    );

                    m_readyBuffer.resize(
                        m_frameBufferSize
                    );

                    m_uploadBuffer.resize(
                        m_frameBufferSize
                    );
                }
                catch (...)
                {
                    Logger::Log(
                        "[Camera] Failed to restore "
                        "frame buffers.\n"
                    );

                    Shutdown();

                    return false;
                }

                // -----------------------------------------
                // Reset frame state
                // -----------------------------------------

                {
                    std::lock_guard<std::mutex> lock(
                        m_frameMutex
                    );

                    m_newFrameAvailable.store(
                        false,
                        std::memory_order_release
                    );
                }

                // -----------------------------------------
                // Reset statistics
                // -----------------------------------------

                m_framesCaptured = 0;
                m_framesUploaded = 0;
                m_framesDropped = 0;

                m_captureFrameBytes = 0;

                m_captureFps = 0.0;
                m_captureFrameTimeMs = 0.0;

                m_uploadFps = 0.0;
                m_uploadFrameTimeMs = 0.0;

                m_uploadStatsTime =
                    std::chrono::steady_clock::time_point{};

                m_uploadStatsFrameCount = 0;

                // -----------------------------------------
                // Restart camera
                // -----------------------------------------

                m_open.store(
                    true,
                    std::memory_order_release
                );

                m_running.store(
                    true,
                    std::memory_order_release
                );

                m_cameraThread =
                    std::thread(
                        &Camera::CameraThread,
                        this
                    );

                Logger::Log(
                    "[Camera] Restored previous "
                    "camera mode.\n"
                );

                return false;
            }

            restoreCamera->Release();
        }

        Logger::Log(
            "[Camera] Failed to restore previous "
            "camera mode.\n"
        );

        m_open.store(
            false,
            std::memory_order_release
        );

        return false;
    }

    camera->Release();

    // -----------------------------------------------------
    // Recreate GPU resources
    // -----------------------------------------------------

    if (!CreateTexture())
    {
        Logger::Log(
            "[Camera] Failed to recreate camera texture "
            "after mode switch.\n"
        );

        m_reader.Reset();

        m_requestedMode =
            previousMode;

        m_open.store(
            false,
            std::memory_order_release
        );

        return false;
    }

    // -----------------------------------------------------
    // Resize CPU buffers
    // -----------------------------------------------------

    try
    {
        m_captureBuffer.resize(
            m_frameBufferSize
        );

        m_readyBuffer.resize(
            m_frameBufferSize
        );

        m_uploadBuffer.resize(
            m_frameBufferSize
        );
    }
    catch (...)
    {
        Logger::Log(
            "[Camera] Failed to resize camera buffers "
            "after mode switch.\n"
        );

        Shutdown();

        return false;
    }

    // -----------------------------------------------------
    // Reset frame state
    // -----------------------------------------------------

    {
        std::lock_guard<std::mutex> lock(
            m_frameMutex
        );

        m_newFrameAvailable.store(
            false,
            std::memory_order_release
        );
    }

    // -----------------------------------------------------
    // Reset statistics
    // -----------------------------------------------------

    m_framesCaptured = 0;
    m_framesUploaded = 0;
    m_framesDropped = 0;

    m_captureFrameBytes = 0;

    m_captureFps = 0.0;
    m_captureFrameTimeMs = 0.0;

    m_uploadFps = 0.0;
    m_uploadFrameTimeMs = 0.0;

    m_uploadStatsTime =
        std::chrono::steady_clock::time_point{};

    m_uploadStatsFrameCount = 0;

    // -----------------------------------------------------
    // Restart camera thread
    // -----------------------------------------------------

    m_open.store(
        true,
        std::memory_order_release
    );

    m_running.store(
        true,
        std::memory_order_release
    );

    m_cameraThread =
        std::thread(
            &Camera::CameraThread,
            this
        );

    Logger::Log(
        "[Camera] Mode switched successfully: "
        "%dx%d @ %u/%u FPS\n",
        m_width,
        m_height,
        m_fpsNumerator,
        m_fpsDenominator
    );

    return true;
}

CameraControls& Camera::Controls()
{
    return m_controls;
}

const CameraControls& Camera::Controls() const
{
    return m_controls;
}

// =========================================================
// Resize
// =========================================================

bool Camera::Resize(
    int width,
    int height
)
{
    if (
        width <= 0 ||
        height <= 0
        )
    {
        return false;
    }

    if (
        width == m_width &&
        height == m_height
        )
    {
        return true;
    }

    Logger::Log(
        "[Camera] Resize requested: %dx%d -> %dx%d.\n",
        m_width,
        m_height,
        width,
        height
    );

    Logger::Log(
        "[Camera] Camera resolution changes must be "
        "negotiated through Media Foundation.\n"
    );

    return false;
}


// =========================================================
// GetTexture
// =========================================================

ID3D11ShaderResourceView*
Camera::GetTexture() const
{
    return m_shaderResourceView.Get();
}


// =========================================================
// GetWidth
// =========================================================

int Camera::GetWidth() const
{
    return m_width;
}


// =========================================================
// GetHeight
// =========================================================

int Camera::GetHeight() const
{
    return m_height;
}


// =========================================================
// GetExpectedNV12FrameSize
// =========================================================

size_t Camera::GetExpectedNV12FrameSize() const
{
    if (
        m_width <= 0 ||
        m_height <= 0
        )
    {
        return 0;
    }

    return
        static_cast<size_t>(m_width) *
        static_cast<size_t>(m_height) *
        3 /
        2;
}


// =========================================================
// GetExpectedBGRASize
// =========================================================

size_t Camera::GetExpectedBGRASize() const
{
    if (
        m_width <= 0 ||
        m_height <= 0
        )
    {
        return 0;
    }

    return
        static_cast<size_t>(m_width) *
        static_cast<size_t>(m_height) *
        4;
}


// =========================================================
// IsNV12
// =========================================================

bool Camera::IsNV12(
    CameraFormat format
)
{
    return format == CameraFormat::NV12;
}


// =========================================================
// IsMJPG
// =========================================================

bool Camera::IsMJPG(
    CameraFormat format
)
{
    return format == CameraFormat::MJPG;
}


// =========================================================
// GetStatistics
// =========================================================

Camera::CameraStatistics
Camera::GetStatistics() const
{
    CameraStatistics stats{};

    stats.width =
        m_width;

    stats.height =
        m_height;

    stats.fpsNumerator =
        m_fpsNumerator;

    stats.fpsDenominator =
        m_fpsDenominator;

    stats.format =
        m_format;

    stats.captureFps =
        m_captureFps.load(
            std::memory_order_relaxed
        );

    stats.captureFrameTimeMs =
        m_captureFrameTimeMs.load(
            std::memory_order_relaxed
        );

    stats.uploadFps =
        m_uploadFps.load(
            std::memory_order_relaxed
        );

    stats.uploadFrameTimeMs =
        m_uploadFrameTimeMs.load(
            std::memory_order_relaxed
        );

    stats.framesCaptured =
        m_framesCaptured.load(
            std::memory_order_relaxed
        );

    stats.framesUploaded =
        m_framesUploaded.load(
            std::memory_order_relaxed
        );

    stats.framesDropped =
        m_framesDropped.load(
            std::memory_order_relaxed
        );

    stats.frameBytes =
        m_captureFrameBytes.load(
            std::memory_order_relaxed
        );

    stats.open =
        m_open.load(
            std::memory_order_acquire
        );

    stats.threadRunning =
        m_running.load(
            std::memory_order_acquire
        );

    return stats;
}


// =========================================================
// IsOpen
// =========================================================

bool Camera::IsOpen() const
{
    return m_open.load(
        std::memory_order_acquire
    );
}


// =========================================================
// Shutdown
// =========================================================

void Camera::Shutdown()
{
    // -----------------------------------------------------
    // Tell camera thread to stop
    // -----------------------------------------------------

    m_open.store(
        false,
        std::memory_order_release
    );

    m_running.store(
        false,
        std::memory_order_release
    );

    // -----------------------------------------------------
    // Wait for camera thread
    //
    // IMPORTANT:
    //
    // Do this before releasing m_reader or m_wicFactory.
    // -----------------------------------------------------

    if (m_cameraThread.joinable())
    {
        m_cameraThread.join();
    }

    // -----------------------------------------------------
    // Release NV12 resources
    // -----------------------------------------------------

    ReleaseNV12Resources();

    // -----------------------------------------------------
    // Release final D3D11 resources
    // -----------------------------------------------------

    m_renderTargetView.Reset();
    m_shaderResourceView.Reset();
    m_texture.Reset();

    // -----------------------------------------------------
    // Release Media Foundation reader
    // -----------------------------------------------------

    m_reader.Reset();

    // -----------------------------------------------------
    // Release WIC
    // -----------------------------------------------------

    m_wicFactory.Reset();

    // -----------------------------------------------------
    // Release CPU buffers
    // -----------------------------------------------------

    {
        std::lock_guard<std::mutex> lock(
            m_frameMutex
        );

        m_captureBuffer.clear();
        m_readyBuffer.clear();
        m_uploadBuffer.clear();

        m_newFrameAvailable.store(
            false,
            std::memory_order_release
        );
    }

    // -----------------------------------------------------
    // Shut down Media Foundation
    // -----------------------------------------------------

    if (m_mediaFoundationStarted)
    {
        MFShutdown();

        m_mediaFoundationStarted =
            false;
    }

    // -----------------------------------------------------
    // Reset state
    // -----------------------------------------------------

    m_availableModes.clear();

    m_requestedMode =
        CameraMode{};

    m_controls.Shutdown();

    m_device = nullptr;
    m_context = nullptr;

    m_cameraIndex = 0;

    m_width = 0;
    m_height = 0;

    m_fpsNumerator = 0;
    m_fpsDenominator = 1;

    m_format =
        CameraFormat::None;

    m_requestedFormat =
        CameraFormat::NV12;

    m_frameBufferSize = 0;

    m_nv12YSize = 0;
    m_nv12UVSize = 0;

    m_framesCaptured = 0;
    m_framesUploaded = 0;
    m_framesDropped = 0;

    m_captureFrameBytes = 0;

    m_captureFps = 0.0;
    m_captureFrameTimeMs = 0.0;

    m_uploadFps = 0.0;
    m_uploadFrameTimeMs = 0.0;

    m_uploadStatsTime =
        std::chrono::steady_clock::time_point{};

    m_uploadStatsFrameCount = 0;

    m_recordedFrames.clear();
    m_recordingFilePath.clear();
}