#pragma once

#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;


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
        return m_width;
    }


    int GetHeight() const
    {
        return m_height;
    }


    bool IsOpen() const
    {
        return m_open;
    }


    const std::string& GetFilePath() const
    {
        return m_filePath;
    }


private:

    bool ReadNextSample(
        ComPtr<IMFSample>& sample,
        LONGLONG& timestamp
    );

    bool UploadSample(
        IMFSample* sample
    );

    bool Seek(
        double time
    );

    bool CreateTexture();


private:

    ID3D11Device*
        m_device = nullptr;

    ID3D11DeviceContext*
        m_context = nullptr;


    ComPtr<IMFSourceReader>
        m_reader;


    ComPtr<ID3D11Texture2D>
        m_texture;

    ComPtr<ID3D11ShaderResourceView>
        m_shaderResourceView;


    std::vector<uint8_t>
        m_frameBuffer;


    int
        m_width = 0;

    int
        m_height = 0;

    LONG
        m_sourceStride = 0;


    double
        m_currentFrameTime = 0.0;


    ComPtr<IMFSample>
        m_pendingSample;

    LONGLONG
        m_pendingTimestamp = 0;


    bool
        m_endOfStream = false;

    bool
        m_open = false;


    std::string
        m_filePath;
};