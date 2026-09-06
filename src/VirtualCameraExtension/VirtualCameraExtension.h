#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <Windows.h>

#include <mfidl.h>
#include <mfvirtualcamera.h>

using Microsoft::WRL::ComPtr;

class VirtualCameraExtension
{
public:
    VirtualCameraExtension() = default;
    ~VirtualCameraExtension();

    VirtualCameraExtension(const VirtualCameraExtension&) = delete;
    VirtualCameraExtension& operator=(const VirtualCameraExtension&) = delete;

    bool Initialize(ID3D11Device* device);
    void Shutdown();

    bool IsAvailable() const;
    bool IsActive() const;
    bool IsRegistered() const;

    bool RegisterExtension() const;
    bool UnregisterExtension() const;

    bool StartVirtualCamera();
    bool StopVirtualCamera();

    // Acquire the shared texture for rendering by PianoVisualizer.
    // Returns false when the Frame Server still owns the previous frame.
    bool AcquireFrame();
    void ReleaseFrame();

    ID3D11Texture2D* GetFrameTexture() const;
    ID3D11RenderTargetView* GetFrameRTV() const;

private:
    using DllRegisterServerFn = HRESULT(STDAPICALLTYPE*)();
    using DllUnregisterServerFn = HRESULT(STDAPICALLTYPE*)();

    bool FindExtensionDll();
    bool CreateSharedTexture(ID3D11Device* device);
    void DestroySharedTexture();

    ComPtr<IMFVirtualCamera> m_virtualCamera;

    ComPtr<ID3D11Texture2D> m_frameTexture;
    ComPtr<ID3D11RenderTargetView> m_frameRTV;
    ComPtr<IDXGIKeyedMutex> m_frameMutex;

    HANDLE m_sharedHandle = nullptr;
    bool m_frameAcquired = false;

    bool m_initialized = false;
    bool m_available = false;
    bool m_active = false;

    wchar_t m_dllPath[MAX_PATH]{};
};
