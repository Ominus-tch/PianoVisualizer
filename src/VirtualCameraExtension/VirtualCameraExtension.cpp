#include "VirtualCameraExtension.h"

#include <filesystem>
#include <sddl.h>

#include <mfapi.h>
#include <mfvirtualcamera.h>

#include <shellapi.h>

#include "../../util/Logger.h"

#include <dxgi1_2.h>

#pragma comment(lib, "Advapi32.lib")

namespace fs = std::filesystem;

namespace
{
    constexpr wchar_t VirtualCameraDllRelativePath[] =
        L"extensions\\VirtualCamera\\VirtualCameraExtension.dll";

    constexpr wchar_t VirtualCameraFriendlyName[] =
        L"Piano Visualizer Virtual Camera";

    constexpr wchar_t VirtualCameraClsid[] =
        L"{8F9C6C1A-4E2D-4B3A-917F-325C7A4E9120}";

    // This resource is deliberately fixed-size for the first GPU-sharing
    // milestone. The camera source currently exposes the same format.
    constexpr UINT32 SharedFrameWidth = 1920;
    constexpr UINT32 SharedFrameHeight = 1080;
    constexpr wchar_t SharedFrameName[] =
        L"Global\\PianoVisualizerVirtualCameraFrame";
}

VirtualCameraExtension::~VirtualCameraExtension()
{
    Shutdown();
}

bool VirtualCameraExtension::Initialize(ID3D11Device* device)
{
    if (m_initialized)
        return m_available;

    m_initialized = true;
    m_available = false;
    m_active = false;

    if (!device)
    {
        Logger::Log(
            "[VirtualCamera] Cannot initialize extension: D3D11 device is null.\n"
        );

        m_initialized = false;
        return false;
    }

    if (!FindExtensionDll())
    {
        Logger::Log(
            "[VirtualCamera] Extension DLL not found. Virtual camera unavailable.\n"
        );

        return false;
    }

    if (!CreateSharedTexture(device))
    {
        Logger::Log(
            "[VirtualCamera] Failed to create shared camera frame texture.\n"
        );

        return false;
    }

    m_available = true;

    Logger::Log(
        "[VirtualCamera] Extension available. Shared frame: %ux%u.\n",
        SharedFrameWidth,
        SharedFrameHeight
    );

    return true;
}

void VirtualCameraExtension::Shutdown()
{
    if (m_frameAcquired)
        ReleaseFrame();

    if (m_active)
    {
        StopVirtualCamera();
    }

    m_virtualCamera.Reset();
    DestroySharedTexture();

    m_available = false;
    m_initialized = false;
    m_dllPath[0] = L'\0';
}

bool VirtualCameraExtension::IsAvailable() const
{
    return m_available;
}

bool VirtualCameraExtension::IsActive() const
{
    return m_active;
}

bool VirtualCameraExtension::IsRegistered() const
{
    if (!m_available || m_dllPath[0] == L'\0')
        return false;

    HKEY key = nullptr;

    const LONG result = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes\\CLSID\\{8F9C6C1A-4E2D-4B3A-917F-325C7A4E9120}\\InProcServer32",
        0,
        KEY_READ | KEY_WOW64_64KEY,
        &key
    );

    if (result != ERROR_SUCCESS)
        return false;

    wchar_t registeredPath[MAX_PATH]{};
    DWORD pathSize = sizeof(registeredPath);
    DWORD type = 0;

    const LONG queryResult = RegQueryValueExW(
        key,
        nullptr,
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(registeredPath),
        &pathSize
    );

    RegCloseKey(key);

    if (
        queryResult != ERROR_SUCCESS ||
        type != REG_SZ
        )
    {
        return false;
    }

    fs::path registeredDllPath(registeredPath);
    fs::path expectedDllPath(m_dllPath);

    std::error_code registeredError;
    std::error_code expectedError;

    const fs::path normalizedRegistered =
        fs::weakly_canonical(
            registeredDllPath,
            registeredError
        );

    const fs::path normalizedExpected =
        fs::weakly_canonical(
            expectedDllPath,
            expectedError
        );

    if (!registeredError && !expectedError)
    {
        return _wcsicmp(
            normalizedRegistered.c_str(),
            normalizedExpected.c_str()
        ) == 0;
    }

    return _wcsicmp(
        registeredPath,
        m_dllPath
    ) == 0;
}

bool VirtualCameraExtension::FindExtensionDll()
{
    wchar_t executablePath[MAX_PATH]{};

    DWORD length = GetModuleFileNameW(
        nullptr,
        executablePath,
        ARRAYSIZE(executablePath)
    );

    if (length == 0 || length >= ARRAYSIZE(executablePath))
    {
        Logger::Log(
            "[VirtualCamera] Failed to get executable path. Win32 error: %lu\n",
            GetLastError()
        );

        return false;
    }

    fs::path exePath(executablePath);
    fs::path exeDirectory = exePath.parent_path();
    fs::path dllPath = exeDirectory / VirtualCameraDllRelativePath;

    std::error_code error;

    if (!fs::exists(dllPath, error) || error)
    {
        Logger::Log(
            "[VirtualCamera] DLL does not exist: %ls\n",
            dllPath.c_str()
        );

        return false;
    }

    if (!fs::is_regular_file(dllPath, error) || error)
    {
        Logger::Log(
            "[VirtualCamera] DLL path is not a regular file: %ls\n",
            dllPath.c_str()
        );

        return false;
    }

    const std::wstring pathString = dllPath.wstring();

    if (pathString.length() >= ARRAYSIZE(m_dllPath))
    {
        Logger::Log(
            "[VirtualCamera] DLL path is too long: %ls\n",
            pathString.c_str()
        );

        return false;
    }

    wcscpy_s(
        m_dllPath,
        ARRAYSIZE(m_dllPath),
        pathString.c_str()
    );

    Logger::Log(
        "[VirtualCamera] Found extension DLL: %ls\n",
        m_dllPath
    );

    return true;
}

bool VirtualCameraExtension::CreateSharedTexture(ID3D11Device* device)
{
    D3D11_TEXTURE2D_DESC description{};

    description.Width = SharedFrameWidth;
    description.Height = SharedFrameHeight;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;
    description.MiscFlags =
        D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
        D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    HRESULT hr = device->CreateTexture2D(
        &description,
        nullptr,
        &m_frameTexture
    );

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualCamera] CreateTexture2D(shared) failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    hr = device->CreateRenderTargetView(
        m_frameTexture.Get(),
        nullptr,
        &m_frameRTV
    );

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualCamera] CreateRenderTargetView(shared) failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        DestroySharedTexture();
        return false;
    }

    hr = m_frameTexture.As(&m_frameMutex);

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualCamera] Failed to query shared keyed mutex. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        DestroySharedTexture();
        return false;
    }

    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;;GA;;;LS)(A;;GA;;;IU)",
        SDDL_REVISION_1,
        &securityDescriptor,
        nullptr
    ))
    {
        Logger::Log(
            "[VirtualCamera] Failed to create shared texture security descriptor. Win32 error: %lu\n",
            GetLastError()
        );

        DestroySharedTexture();
        return false;
    }

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.lpSecurityDescriptor = securityDescriptor;
    securityAttributes.bInheritHandle = FALSE;

    ComPtr<IDXGIResource1> resource;

    hr = m_frameTexture.As(&resource);

    if (SUCCEEDED(hr))
    {
        hr = resource->CreateSharedHandle(
            &securityAttributes,
            DXGI_SHARED_RESOURCE_READ |
            DXGI_SHARED_RESOURCE_WRITE,
            SharedFrameName,
            &m_sharedHandle
        );
    }

    LocalFree(securityDescriptor);

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualCamera] CreateSharedHandle failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        DestroySharedTexture();
        return false;
    }

    Logger::Log(
        "[VirtualCamera] Created shared frame resource: %ls\n",
        SharedFrameName
    );

    return true;
}

void VirtualCameraExtension::DestroySharedTexture()
{
    m_frameAcquired = false;

    if (m_sharedHandle)
    {
        CloseHandle(m_sharedHandle);
        m_sharedHandle = nullptr;
    }

    m_frameMutex.Reset();
    m_frameRTV.Reset();
    m_frameTexture.Reset();
}

bool VirtualCameraExtension::RegisterExtension() const
{
    if (!m_available)
    {
        Logger::Log(
            "[VirtualCamera] Cannot register extension because it is unavailable.\n"
        );

        return false;
    }

    if (m_active)
    {
        Logger::Log(
            "[VirtualCamera] Cannot register extension while virtual camera is active.\n"
        );

        return false;
    }

    if (IsRegistered())
    {
        Logger::Log(
            "[VirtualCamera] Extension is already registered.\n"
        );

        return true;
    }

    wchar_t systemDirectory[MAX_PATH]{};

    UINT length = GetSystemDirectoryW(
        systemDirectory,
        ARRAYSIZE(systemDirectory)
    );

    if (length == 0 || length >= ARRAYSIZE(systemDirectory))
    {
        Logger::Log(
            "[VirtualCamera] Failed to get system directory. Win32 error: %lu\n",
            GetLastError()
        );

        return false;
    }

    const std::wstring regsvr32Path =
        std::wstring(systemDirectory) +
        L"\\regsvr32.exe";

    const std::wstring parameters =
        L"/s \"" +
        std::wstring(m_dllPath) +
        L"\"";

    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.lpVerb = L"runas";
    executeInfo.lpFile = regsvr32Path.c_str();
    executeInfo.lpParameters = parameters.c_str();
    executeInfo.nShow = SW_HIDE;

    if (!ShellExecuteExW(&executeInfo))
    {
        Logger::Log(
            "[VirtualCamera] Failed to start elevated registration. Win32 error: %lu\n",
            GetLastError()
        );

        return false;
    }

    WaitForSingleObject(
        executeInfo.hProcess,
        INFINITE
    );

    DWORD exitCode = 0;

    if (!GetExitCodeProcess(
        executeInfo.hProcess,
        &exitCode
    ))
    {
        Logger::Log(
            "[VirtualCamera] Failed to get registration process exit code. Win32 error: %lu\n",
            GetLastError()
        );

        CloseHandle(executeInfo.hProcess);
        return false;
    }

    CloseHandle(executeInfo.hProcess);

    if (exitCode != 0)
    {
        Logger::Log(
            "[VirtualCamera] regsvr32 registration failed. Exit code: %lu\n",
            exitCode
        );

        return false;
    }

    if (!IsRegistered())
    {
        Logger::Log(
            "[VirtualCamera] Registration process succeeded, but extension is not registered.\n"
        );

        return false;
    }

    Logger::Log(
        "[VirtualCamera] Extension registered successfully.\n"
    );

    return true;
}

bool VirtualCameraExtension::UnregisterExtension() const
{
    if (!m_available)
    {
        Logger::Log(
            "[VirtualCamera] Cannot unregister extension because it is unavailable.\n"
        );

        return false;
    }

    if (m_active)
    {
        Logger::Log(
            "[VirtualCamera] Cannot unregister extension while virtual camera is active.\n"
        );

        return false;
    }

    if (!IsRegistered())
    {
        Logger::Log(
            "[VirtualCamera] Extension is already not registered.\n"
        );

        return true;
    }

    wchar_t systemDirectory[MAX_PATH]{};

    UINT length = GetSystemDirectoryW(
        systemDirectory,
        ARRAYSIZE(systemDirectory)
    );

    if (length == 0 || length >= ARRAYSIZE(systemDirectory))
    {
        Logger::Log(
            "[VirtualCamera] Failed to get system directory. Win32 error: %lu\n",
            GetLastError()
        );

        return false;
    }

    const std::wstring regsvr32Path =
        std::wstring(systemDirectory) +
        L"\\regsvr32.exe";

    const std::wstring parameters =
        L"/s /u \"" +
        std::wstring(m_dllPath) +
        L"\"";

    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.lpVerb = L"runas";
    executeInfo.lpFile = regsvr32Path.c_str();
    executeInfo.lpParameters = parameters.c_str();
    executeInfo.nShow = SW_HIDE;

    if (!ShellExecuteExW(&executeInfo))
    {
        Logger::Log(
            "[VirtualCamera] Failed to start elevated unregistration. Win32 error: %lu\n",
            GetLastError()
        );

        return false;
    }

    WaitForSingleObject(
        executeInfo.hProcess,
        INFINITE
    );

    DWORD exitCode = 0;

    if (!GetExitCodeProcess(
        executeInfo.hProcess,
        &exitCode
    ))
    {
        Logger::Log(
            "[VirtualCamera] Failed to get unregistration process exit code. Win32 error: %lu\n",
            GetLastError()
        );

        CloseHandle(executeInfo.hProcess);
        return false;
    }

    CloseHandle(executeInfo.hProcess);

    if (exitCode != 0)
    {
        Logger::Log(
            "[VirtualCamera] regsvr32 unregistration failed. Exit code: %lu\n",
            exitCode
        );

        return false;
    }

    if (IsRegistered())
    {
        Logger::Log(
            "[VirtualCamera] Unregistration process succeeded, but extension is still registered.\n"
        );

        return false;
    }

    Logger::Log(
        "[VirtualCamera] Extension unregistered successfully.\n"
    );

    return true;
}

bool VirtualCameraExtension::StartVirtualCamera()
{
    if (!m_available)
    {
        Logger::Log(
            "[VirtualCamera] Cannot start virtual camera because the extension is unavailable.\n"
        );

        return false;
    }

    if (m_active)
    {
        Logger::Log(
            "[VirtualCamera] Virtual camera is already active.\n"
        );

        return true;
    }

    if (!IsRegistered())
    {
        Logger::Log(
            "[VirtualCamera] Cannot start virtual camera because the extension is not registered.\n"
        );

        return false;
    }

    HRESULT hr = MFStartup(
        MF_VERSION,
        MFSTARTUP_FULL
    );

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualCamera] MFStartup failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    hr = MFCreateVirtualCamera(
        MFVirtualCameraType_SoftwareCameraSource,
        MFVirtualCameraLifetime_Session,
        MFVirtualCameraAccess_CurrentUser,
        VirtualCameraFriendlyName,
        VirtualCameraClsid,
        nullptr,
        0,
        &m_virtualCamera
    );

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualCamera] MFCreateVirtualCamera failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        MFShutdown();
        return false;
    }

    hr = m_virtualCamera->Start(nullptr);

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualCamera] Virtual camera Start failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        m_virtualCamera.Reset();
        MFShutdown();
        return false;
    }

    m_active = true;

    Logger::Log(
        "[VirtualCamera] Virtual camera started successfully.\n"
    );

    return true;
}

bool VirtualCameraExtension::StopVirtualCamera()
{
    if (!m_active)
        return true;

    if (!m_virtualCamera)
    {
        Logger::Log(
            "[VirtualCamera] Virtual camera is active but interface is missing.\n"
        );

        m_active = false;
        MFShutdown();
        return false;
    }

    HRESULT hr = m_virtualCamera->Stop();

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualCamera] Virtual camera Stop failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    m_virtualCamera.Reset();

    if (m_frameAcquired)
        ReleaseFrame();

    MFShutdown();

    m_active = false;

    Logger::Log(
        "[VirtualCamera] Virtual camera stopped successfully.\n"
    );

    return true;
}

bool VirtualCameraExtension::AcquireFrame()
{
    if (!m_active || !m_frameMutex)
        return false;

    if (m_frameAcquired)
        return true;

    const HRESULT hr = m_frameMutex->AcquireSync(
        0,
        0
    );

    if (hr == WAIT_ABANDONED)
    {
        Logger::Log(
            "[VirtualCamera] Shared frame mutex was abandoned; reclaiming frame.\n"
        );

        m_frameAcquired = true;
        return true;
    }

    if (hr == WAIT_TIMEOUT)
        return false;

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualCamera] AcquireSync failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    m_frameAcquired = true;
    return true;
}

void VirtualCameraExtension::ReleaseFrame()
{
    if (!m_frameAcquired || !m_frameMutex)
        return;

    const HRESULT hr = m_frameMutex->ReleaseSync(1);

    if (FAILED(hr))
    {
        Logger::Log(
            "[VirtualCamera] ReleaseSync failed. HRESULT: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );
    }

    m_frameAcquired = false;
}

ID3D11Texture2D* VirtualCameraExtension::GetFrameTexture() const
{
    return m_frameTexture.Get();
}

ID3D11RenderTargetView* VirtualCameraExtension::GetFrameRTV() const
{
    return m_frameRTV.Get();
}
