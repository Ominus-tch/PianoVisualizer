#include "CameraControls.h"

#include "../logger.h"

#include <vector>

#pragma comment(lib, "ksproxy.lib")

CameraControls::~CameraControls()
{
    Shutdown();
}

bool CameraControls::Initialize(
    IMFMediaSource* source
)
{
    Shutdown();

    if (!source)
    {
        return false;
    }

    HRESULT hr =
        source->QueryInterface(
            IID_PPV_ARGS(
                &m_ksControl
            )
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "Failed to acquire IKsControl: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    Logger::Log(
        "[CameraControls] IKsControl initialized\n"
    );

    return true;
}

void CameraControls::Shutdown()
{
    m_ksControl.Reset();
}

bool CameraControls::IsInitialized() const
{
    return m_ksControl != nullptr;
}

bool CameraControls::GetProperty(
    ULONG propertyId,
    ULONG& value
) const
{
    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY property{};

    property.Set =
        PROPSETID_VIDCAP_CAMERACONTROL;

    property.Id =
        propertyId;

    property.Flags =
        KSPROPERTY_TYPE_GET;

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property,
            sizeof(property),
            &value,
            sizeof(value),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        return false;
    }

    return bytesReturned >= sizeof(ULONG);
}

bool CameraControls::SetProperty(
    ULONG propertyId,
    ULONG value
)
{
    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY property{};

    property.Set =
        PROPSETID_VIDCAP_CAMERACONTROL;

    property.Id =
        propertyId;

    property.Flags =
        KSPROPERTY_TYPE_SET;

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property,
            sizeof(property),
            &value,
            sizeof(value),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "KsProperty SET failed "
            "(property=%lu, hr=0x%08X)\n",
            propertyId,
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    return true;
}

bool CameraControls::GetAutoExposure(
    bool& enabled
) const
{
    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY_CAMERACONTROL_S property{};

    property.Property.Set =
        PROPSETID_VIDCAP_CAMERACONTROL;

    property.Property.Id =
        KSPROPERTY_CAMERACONTROL_EXPOSURE;

    property.Property.Flags =
        KSPROPERTY_TYPE_GET;

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property.Property,
            sizeof(property.Property),
            &property,
            sizeof(property),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "GetAutoExposure failed: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    enabled =
        (property.Flags &
            KSPROPERTY_CAMERACONTROL_FLAGS_AUTO) != 0;

    return true;
}

bool CameraControls::SetAutoExposure(
    bool enabled
)
{
    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY_CAMERACONTROL_S property{};

    property.Property.Set =
        PROPSETID_VIDCAP_CAMERACONTROL;

    property.Property.Id =
        KSPROPERTY_CAMERACONTROL_EXPOSURE;

    property.Property.Flags =
        KSPROPERTY_TYPE_SET;

    property.Value = 0;

    property.Flags =
        enabled
        ? KSPROPERTY_CAMERACONTROL_FLAGS_AUTO
        : KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property.Property,
            sizeof(property.Property),
            &property,
            sizeof(property),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "SetAutoExposure failed: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    return true;
}

bool CameraControls::GetExposure(
    int& exposure
) const
{
    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY_CAMERACONTROL_S property{};

    property.Property.Set =
        PROPSETID_VIDCAP_CAMERACONTROL;

    property.Property.Id =
        KSPROPERTY_CAMERACONTROL_EXPOSURE;

    property.Property.Flags =
        KSPROPERTY_TYPE_GET;

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property.Property,
            sizeof(property.Property),
            &property,
            sizeof(property),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "GetExposure failed: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    exposure =
        static_cast<int>(property.Value);

    return true;
}

bool CameraControls::SetExposure(
    int exposure
)
{
    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY_CAMERACONTROL_S property{};

    property.Property.Set =
        PROPSETID_VIDCAP_CAMERACONTROL;

    property.Property.Id =
        KSPROPERTY_CAMERACONTROL_EXPOSURE;

    property.Property.Flags =
        KSPROPERTY_TYPE_SET;

    property.Value =
        static_cast<LONG>(exposure);

    property.Flags =
        KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property.Property,
            sizeof(property.Property),
            &property,
            sizeof(property),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "SetExposure failed: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    return true;
}


bool CameraControls::GetExposureRange(
    int& minimum,
    int& maximum,
    int& step
)
{
    minimum = 0;
    maximum = 0;
    step = 1;

	if (m_exposureMax != 0 && m_exposureMin != 0)
	{
		minimum = m_exposureMin;
		maximum = m_exposureMax;
		step = m_exposureStep;
		return true;
	}

    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY property{};

    property.Set =
        PROPSETID_VIDCAP_CAMERACONTROL;

    property.Id =
        KSPROPERTY_CAMERACONTROL_EXPOSURE;

    property.Flags =
        KSPROPERTY_TYPE_BASICSUPPORT;

    std::vector<BYTE> buffer(
        1024
    );

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property,
            sizeof(property),
            buffer.data(),
            static_cast<ULONG>(
                buffer.size()
                ),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "Exposure basic support failed: "
            "0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    if (
        bytesReturned <
        sizeof(KSPROPERTY_DESCRIPTION)
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Exposure basic support response "
            "too small: %lu bytes\n",
            bytesReturned
        );

        return false;
    }

    const auto* description =
        reinterpret_cast<
        const KSPROPERTY_DESCRIPTION*
        >(
            buffer.data()
            );

    if (
        description->DescriptionSize <
        sizeof(KSPROPERTY_DESCRIPTION)
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Invalid exposure description size: %lu\n",
            description->DescriptionSize
        );

        return false;
    }

    const size_t membersOffset =
        sizeof(KSPROPERTY_DESCRIPTION);

    if (
        bytesReturned <
        membersOffset +
        sizeof(KSPROPERTY_MEMBERSHEADER)
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Exposure members header is missing\n"
        );

        return false;
    }

    const auto* members =
        reinterpret_cast<
        const KSPROPERTY_MEMBERSHEADER*
        >(
            buffer.data() +
            membersOffset
            );

    if (
        (members->MembersFlags &
            KSPROPERTY_MEMBER_RANGES) == 0
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Exposure property does not report ranges\n"
        );

        return false;
    }

    if (
        members->MembersCount == 0 ||
        members->MembersSize == 0
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Exposure range contains no members\n"
        );

        return false;
    }

    const size_t rangeOffset =
        membersOffset +
        sizeof(KSPROPERTY_MEMBERSHEADER);

    const size_t rangeSize =
        static_cast<size_t>(
            members->MembersSize
            );

    const size_t requiredSize =
        rangeOffset +
        rangeSize *
        static_cast<size_t>(
            members->MembersCount
            );

    if (
        bytesReturned <
        requiredSize
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Exposure range data is incomplete: "
            "need=%zu, got=%lu\n",
            requiredSize,
            bytesReturned
        );

        return false;
    }

    if (
        rangeSize <
        sizeof(KSPROPERTY_STEPPING_LONG)
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Exposure range member is too small: "
            "%zu bytes\n",
            rangeSize
        );

        return false;
    }

    const auto* range =
        reinterpret_cast<
        const KSPROPERTY_STEPPING_LONG*
        >(
            buffer.data() +
            rangeOffset
            );

    minimum =
        static_cast<int>(
            range->Bounds.SignedMinimum
            );

    maximum =
        static_cast<int>(
            range->Bounds.SignedMaximum
            );

    step =
        static_cast<int>(
            range->SteppingDelta
            );

    if (step <= 0)
    {
        step = 1;
    }

	m_exposureMin = minimum;
	m_exposureMax = maximum;
    m_exposureStep = step;

    return true;
}

bool CameraControls::GetLowLightCompensation(
    bool& enabled
) const
{
    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY_CAMERACONTROL_S property{};

    property.Property.Set =
        PROPSETID_VIDCAP_CAMERACONTROL;

    property.Property.Id =
        KSPROPERTY_CAMERACONTROL_AUTO_EXPOSURE_PRIORITY;

    property.Property.Flags =
        KSPROPERTY_TYPE_GET;

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property.Property,
            sizeof(property.Property),
            &property,
            sizeof(property),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "GetLowLightCompensation failed: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    enabled =
        property.Value != 0;

    return true;
}

bool CameraControls::SetLowLightCompensation(
    bool enabled
)
{
    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY_CAMERACONTROL_S property{};

    property.Property.Set =
        PROPSETID_VIDCAP_CAMERACONTROL;

    property.Property.Id =
        KSPROPERTY_CAMERACONTROL_AUTO_EXPOSURE_PRIORITY;

    property.Property.Flags =
        KSPROPERTY_TYPE_SET;

    property.Value =
        enabled ? 1 : 0;

    property.Flags =
        KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property.Property,
            sizeof(property.Property),
            &property,
            sizeof(property),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "SetLowLightCompensation failed: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    return true;
}

// =========================================================
// Auto Focus
// =========================================================

bool CameraControls::GetAutoFocus(
    bool& enabled
) const
{
    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY_CAMERACONTROL_S property{};

    property.Property.Set =
        PROPSETID_VIDCAP_CAMERACONTROL;

    property.Property.Id =
        KSPROPERTY_CAMERACONTROL_FOCUS;

    property.Property.Flags =
        KSPROPERTY_TYPE_GET;

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property.Property,
            sizeof(property.Property),
            &property,
            sizeof(property),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "GetAutoFocus failed: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    enabled =
        (property.Flags &
            KSPROPERTY_CAMERACONTROL_FLAGS_AUTO) != 0;

    return true;
}


bool CameraControls::SetAutoFocus(
    bool enabled
)
{
    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY_CAMERACONTROL_S property{};

    property.Property.Set =
        PROPSETID_VIDCAP_CAMERACONTROL;

    property.Property.Id =
        KSPROPERTY_CAMERACONTROL_FOCUS;

    property.Property.Flags =
        KSPROPERTY_TYPE_SET;

    property.Value = 0;

    property.Flags =
        enabled
        ? KSPROPERTY_CAMERACONTROL_FLAGS_AUTO
        : KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property.Property,
            sizeof(property.Property),
            &property,
            sizeof(property),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "SetAutoFocus failed: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    return true;
}


// =========================================================
// Focus
// =========================================================

bool CameraControls::GetFocus(
    int& focus
) const
{
    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY_CAMERACONTROL_S property{};

    property.Property.Set =
        PROPSETID_VIDCAP_CAMERACONTROL;

    property.Property.Id =
        KSPROPERTY_CAMERACONTROL_FOCUS;

    property.Property.Flags =
        KSPROPERTY_TYPE_GET;

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property.Property,
            sizeof(property.Property),
            &property,
            sizeof(property),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "GetFocus failed: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    focus =
        static_cast<int>(property.Value);

    return true;
}


bool CameraControls::SetFocus(
    int focus
)
{
    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY_CAMERACONTROL_S property{};

    property.Property.Set =
        PROPSETID_VIDCAP_CAMERACONTROL;

    property.Property.Id =
        KSPROPERTY_CAMERACONTROL_FOCUS;

    property.Property.Flags =
        KSPROPERTY_TYPE_SET;

    property.Value =
        static_cast<LONG>(focus);

    property.Flags =
        KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property.Property,
            sizeof(property.Property),
            &property,
            sizeof(property),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "SetFocus failed: 0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    return true;
}


// =========================================================
// Focus Range
// =========================================================

bool CameraControls::GetFocusRange(
    int& minimum,
    int& maximum,
    int& step
)
{
    minimum = 0;
    maximum = 0;
    step = 1;

    if (m_focusMax != 0 && m_focusMin != 0)
    {
        minimum = m_focusMin;
        maximum = m_focusMax;
        step = m_focusStep;

        return true;
    }

    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY property{};

    property.Set =
        PROPSETID_VIDCAP_CAMERACONTROL;

    property.Id =
        KSPROPERTY_CAMERACONTROL_FOCUS;

    property.Flags =
        KSPROPERTY_TYPE_BASICSUPPORT;

    std::vector<BYTE> buffer(
        1024
    );

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property,
            sizeof(property),
            buffer.data(),
            static_cast<ULONG>(
                buffer.size()
                ),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "Focus basic support failed: "
            "0x%08X\n",
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    if (
        bytesReturned <
        sizeof(KSPROPERTY_DESCRIPTION)
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Focus basic support response "
            "too small: %lu bytes\n",
            bytesReturned
        );

        return false;
    }

    const auto* description =
        reinterpret_cast<
        const KSPROPERTY_DESCRIPTION*
        >(
            buffer.data()
            );

    if (
        description->DescriptionSize <
        sizeof(KSPROPERTY_DESCRIPTION)
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Invalid focus description size: %lu\n",
            description->DescriptionSize
        );

        return false;
    }

    const size_t membersOffset =
        sizeof(KSPROPERTY_DESCRIPTION);

    if (
        bytesReturned <
        membersOffset +
        sizeof(KSPROPERTY_MEMBERSHEADER)
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Focus members header is missing\n"
        );

        return false;
    }

    const auto* members =
        reinterpret_cast<
        const KSPROPERTY_MEMBERSHEADER*
        >(
            buffer.data() +
            membersOffset
            );

    if (
        (members->MembersFlags &
            KSPROPERTY_MEMBER_RANGES) == 0
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Focus property does not report ranges\n"
        );

        return false;
    }

    if (
        members->MembersCount == 0 ||
        members->MembersSize == 0
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Focus range contains no members\n"
        );

        return false;
    }

    const size_t rangeOffset =
        membersOffset +
        sizeof(KSPROPERTY_MEMBERSHEADER);

    const size_t rangeSize =
        static_cast<size_t>(
            members->MembersSize
            );

    const size_t requiredSize =
        rangeOffset +
        rangeSize *
        static_cast<size_t>(
            members->MembersCount
            );

    if (
        bytesReturned <
        requiredSize
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Focus range data is incomplete: "
            "need=%zu, got=%lu\n",
            requiredSize,
            bytesReturned
        );

        return false;
    }

    if (
        rangeSize <
        sizeof(KSPROPERTY_STEPPING_LONG)
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Focus range member is too small: "
            "%zu bytes\n",
            rangeSize
        );

        return false;
    }

    const auto* range =
        reinterpret_cast<
        const KSPROPERTY_STEPPING_LONG*
        >(
            buffer.data() +
            rangeOffset
            );

    minimum =
        static_cast<int>(
            range->Bounds.SignedMinimum
            );

    maximum =
        static_cast<int>(
            range->Bounds.SignedMaximum
            );

    step =
        static_cast<int>(
            range->SteppingDelta
            );

    if (step <= 0)
    {
        step = 1;
    }

    m_focusMin = minimum;
    m_focusMax = maximum;
    m_focusStep = step;

    return true;
}

// =========================================================
// Video Proc Amp
// =========================================================

bool CameraControls::GetVideoProcAmpProperty(
    ULONG propertyId,
    int& value
) const
{
    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY_VIDEOPROCAMP_S property{};

    property.Property.Set =
        PROPSETID_VIDCAP_VIDEOPROCAMP;

    property.Property.Id =
        propertyId;

    property.Property.Flags =
        KSPROPERTY_TYPE_GET;

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            reinterpret_cast<PKSPROPERTY>(
                &property
                ),
            sizeof(property),
            &property,
            sizeof(property),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "GetVideoProcAmpProperty failed "
            "(property=%lu, hr=0x%08X, bytes=%lu)\n",
            propertyId,
            static_cast<unsigned int>(hr),
            bytesReturned
        );

        return false;
    }

    if (bytesReturned < sizeof(property))
    {
        Logger::Log(
            "[CameraControls] "
            "GetVideoProcAmpProperty returned too little data "
            "(property=%lu, bytes=%lu)\n",
            propertyId,
            bytesReturned
        );

        return false;
    }

    value =
        static_cast<int>(
            property.Value
            );

    return true;
}


bool CameraControls::SetVideoProcAmpProperty(
    ULONG propertyId,
    int value
)
{
    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY_VIDEOPROCAMP_S property{};

    property.Property.Set =
        PROPSETID_VIDCAP_VIDEOPROCAMP;

    property.Property.Id =
        propertyId;

    property.Property.Flags =
        KSPROPERTY_TYPE_SET;

    property.Value =
        static_cast<LONG>(value);

    property.Flags =
        KSPROPERTY_VIDEOPROCAMP_FLAGS_MANUAL;

    property.Capabilities =
        KSPROPERTY_VIDEOPROCAMP_FLAGS_MANUAL;

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            reinterpret_cast<PKSPROPERTY>(
                &property
                ),
            sizeof(property),
            &property,
            sizeof(property),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "SetVideoProcAmpProperty failed "
            "(property=%lu, value=%d, hr=0x%08X, bytes=%lu)\n",
            propertyId,
            value,
            static_cast<unsigned int>(hr),
            bytesReturned
        );

        return false;
    }

    return true;
}


bool CameraControls::GetVideoProcAmpRange(
    ULONG propertyId,
    int& minimum,
    int& maximum,
    int& step
)
{
    minimum = 0;
    maximum = 0;
    step = 1;

    if (!m_ksControl)
    {
        return false;
    }

    KSPROPERTY property{};

    property.Set =
        PROPSETID_VIDCAP_VIDEOPROCAMP;

    property.Id =
        propertyId;

    property.Flags =
        KSPROPERTY_TYPE_BASICSUPPORT;

    std::vector<BYTE> buffer(
        1024
    );

    ULONG bytesReturned = 0;

    HRESULT hr =
        m_ksControl->KsProperty(
            &property,
            sizeof(property),
            buffer.data(),
            static_cast<ULONG>(
                buffer.size()
                ),
            &bytesReturned
        );

    if (FAILED(hr))
    {
        Logger::Log(
            "[CameraControls] "
            "VideoProcAmp basic support failed "
            "(property=%lu, hr=0x%08X)\n",
            propertyId,
            static_cast<unsigned int>(hr)
        );

        return false;
    }

    if (
        bytesReturned <
        sizeof(KSPROPERTY_DESCRIPTION)
        )
    {
        Logger::Log(
            "[CameraControls] "
            "VideoProcAmp basic support response "
            "too small "
            "(property=%lu, bytes=%lu)\n",
            propertyId,
            bytesReturned
        );

        return false;
    }

    const auto* description =
        reinterpret_cast<
        const KSPROPERTY_DESCRIPTION*
        >(
            buffer.data()
            );

    if (
        description->DescriptionSize <
        sizeof(KSPROPERTY_DESCRIPTION)
        )
    {
        Logger::Log(
            "[CameraControls] "
            "Invalid VideoProcAmp description size "
            "(property=%lu, size=%lu)\n",
            propertyId,
            description->DescriptionSize
        );

        return false;
    }

    const size_t membersOffset =
        sizeof(KSPROPERTY_DESCRIPTION);

    if (
        bytesReturned <
        membersOffset +
        sizeof(KSPROPERTY_MEMBERSHEADER)
        )
    {
        Logger::Log(
            "[CameraControls] "
            "VideoProcAmp members header is missing "
            "(property=%lu)\n",
            propertyId
        );

        return false;
    }

    const auto* members =
        reinterpret_cast<
        const KSPROPERTY_MEMBERSHEADER*
        >(
            buffer.data() +
            membersOffset
            );

    if (
        (members->MembersFlags &
            KSPROPERTY_MEMBER_RANGES) == 0
        )
    {
        Logger::Log(
            "[CameraControls] "
            "VideoProcAmp property does not report ranges "
            "(property=%lu)\n",
            propertyId
        );

        return false;
    }

    if (
        members->MembersCount == 0 ||
        members->MembersSize == 0
        )
    {
        Logger::Log(
            "[CameraControls] "
            "VideoProcAmp range contains no members "
            "(property=%lu)\n",
            propertyId
        );

        return false;
    }

    const size_t rangeOffset =
        membersOffset +
        sizeof(KSPROPERTY_MEMBERSHEADER);

    const size_t rangeSize =
        static_cast<size_t>(
            members->MembersSize
            );

    const size_t requiredSize =
        rangeOffset +
        rangeSize *
        static_cast<size_t>(
            members->MembersCount
            );

    if (
        bytesReturned <
        requiredSize
        )
    {
        Logger::Log(
            "[CameraControls] "
            "VideoProcAmp range data is incomplete "
            "(property=%lu, need=%zu, got=%lu)\n",
            propertyId,
            requiredSize,
            bytesReturned
        );

        return false;
    }

    if (
        rangeSize <
        sizeof(KSPROPERTY_STEPPING_LONG)
        )
    {
        Logger::Log(
            "[CameraControls] "
            "VideoProcAmp range member is too small "
            "(property=%lu, size=%zu)\n",
            propertyId,
            rangeSize
        );

        return false;
    }

    const auto* range =
        reinterpret_cast<
        const KSPROPERTY_STEPPING_LONG*
        >(
            buffer.data() +
            rangeOffset
            );

    minimum =
        static_cast<int>(
            range->Bounds.SignedMinimum
            );

    maximum =
        static_cast<int>(
            range->Bounds.SignedMaximum
            );

    step =
        static_cast<int>(
            range->SteppingDelta
            );

    if (step <= 0)
    {
        step = 1;
    }

    return true;
}


// =========================================================
// Brightness
// =========================================================

bool CameraControls::GetBrightness(
    int& value
) const
{
    return GetVideoProcAmpProperty(
        KSPROPERTY_VIDEOPROCAMP_BRIGHTNESS,
        value
    );
}


bool CameraControls::SetBrightness(
    int value
)
{
    return SetVideoProcAmpProperty(
        KSPROPERTY_VIDEOPROCAMP_BRIGHTNESS,
        value
    );
}


bool CameraControls::GetBrightnessRange(
    int& minimum,
    int& maximum,
    int& step
)
{
    if (
        m_brightnessMax != 0 ||
        m_brightnessMin != 0
        )
    {
        minimum = m_brightnessMin;
        maximum = m_brightnessMax;
        step = m_brightnessStep;

        return true;
    }

    if (!GetVideoProcAmpRange(
        KSPROPERTY_VIDEOPROCAMP_BRIGHTNESS,
        minimum,
        maximum,
        step
    ))
    {
        return false;
    }

    m_brightnessMin = minimum;
    m_brightnessMax = maximum;
    m_brightnessStep = step;

    return true;
}


// =========================================================
// Contrast
// =========================================================

bool CameraControls::GetContrast(
    int& value
) const
{
    return GetVideoProcAmpProperty(
        KSPROPERTY_VIDEOPROCAMP_CONTRAST,
        value
    );
}


bool CameraControls::SetContrast(
    int value
)
{
    return SetVideoProcAmpProperty(
        KSPROPERTY_VIDEOPROCAMP_CONTRAST,
        value
    );
}


bool CameraControls::GetContrastRange(
    int& minimum,
    int& maximum,
    int& step
)
{
    if (
        m_contrastMax != 0 ||
        m_contrastMin != 0
        )
    {
        minimum = m_contrastMin;
        maximum = m_contrastMax;
        step = m_contrastStep;

        return true;
    }

    if (!GetVideoProcAmpRange(
        KSPROPERTY_VIDEOPROCAMP_CONTRAST,
        minimum,
        maximum,
        step
    ))
    {
        return false;
    }

    m_contrastMin = minimum;
    m_contrastMax = maximum;
    m_contrastStep = step;

    return true;
}


// =========================================================
// Saturation
// =========================================================

bool CameraControls::GetSaturation(
    int& value
) const
{
    return GetVideoProcAmpProperty(
        KSPROPERTY_VIDEOPROCAMP_SATURATION,
        value
    );
}


bool CameraControls::SetSaturation(
    int value
)
{
    return SetVideoProcAmpProperty(
        KSPROPERTY_VIDEOPROCAMP_SATURATION,
        value
    );
}


bool CameraControls::GetSaturationRange(
    int& minimum,
    int& maximum,
    int& step
)
{
    if (
        m_saturationMax != 0 ||
        m_saturationMin != 0
        )
    {
        minimum = m_saturationMin;
        maximum = m_saturationMax;
        step = m_saturationStep;

        return true;
    }

    if (!GetVideoProcAmpRange(
        KSPROPERTY_VIDEOPROCAMP_SATURATION,
        minimum,
        maximum,
        step
    ))
    {
        return false;
    }

    m_saturationMin = minimum;
    m_saturationMax = maximum;
    m_saturationStep = step;

    return true;
}


// =========================================================
// Sharpness
// =========================================================

bool CameraControls::GetSharpness(
    int& value
) const
{
    return GetVideoProcAmpProperty(
        KSPROPERTY_VIDEOPROCAMP_SHARPNESS,
        value
    );
}


bool CameraControls::SetSharpness(
    int value
)
{
    return SetVideoProcAmpProperty(
        KSPROPERTY_VIDEOPROCAMP_SHARPNESS,
        value
    );
}


bool CameraControls::GetSharpnessRange(
    int& minimum,
    int& maximum,
    int& step
)
{
    if (
        m_sharpnessMax != 0 ||
        m_sharpnessMin != 0
        )
    {
        minimum = m_sharpnessMin;
        maximum = m_sharpnessMax;
        step = m_sharpnessStep;

        return true;
    }

    if (!GetVideoProcAmpRange(
        KSPROPERTY_VIDEOPROCAMP_SHARPNESS,
        minimum,
        maximum,
        step
    ))
    {
        return false;
    }

    m_sharpnessMin = minimum;
    m_sharpnessMax = maximum;
    m_sharpnessStep = step;

    return true;
}