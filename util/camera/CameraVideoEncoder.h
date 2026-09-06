#pragma once

#include <windows.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <wrl/client.h>

#include <cstdint>
#include <cstddef>
#include <string>


class CameraVideoEncoder
{
public:

    enum class PixelFormat
    {
        NV12,
        RGB32
    };


    CameraVideoEncoder();

    ~CameraVideoEncoder();


    bool Start(
        const std::string& filePath,
        int width,
        int height,
        int fps,
        PixelFormat format
    );


    bool EncodeFrame(
        const uint8_t* pixels,
        size_t pixelSize,
        int64_t timestamp100ns
    );


    void Stop();


    bool IsRecording() const;


private:

    bool CreateEncoder(
        const std::wstring& filePath,
        PixelFormat format
    );


private:

    Microsoft::WRL::ComPtr<IMFSinkWriter>
        m_sinkWriter;

    DWORD
        m_videoStreamIndex = 0;


    int
        m_width = 0;

    int
        m_height = 0;

    int
        m_fps = 0;


    PixelFormat
        m_format = PixelFormat::NV12;


    bool
        m_recording = false;
};