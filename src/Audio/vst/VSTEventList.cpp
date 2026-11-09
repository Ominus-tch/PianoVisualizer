#include "VSTEventList.h"

#include "pluginterfaces/base/funknownimpl.h"

#include "../../../util/Logger.h"

namespace vst
{

    VSTEventList::VSTEventList()
        : _refCount(1)
    {
    }


    Steinberg::tresult PLUGIN_API VSTEventList::queryInterface(
        const Steinberg::TUID iid,
        void** obj)
    {
        if (!obj)
            return Steinberg::kInvalidArgument;

        *obj = nullptr;

        if (Steinberg::FUnknownPrivate::iidEqual(
            iid,
            Steinberg::Vst::IEventList::iid))
        {
            *obj =
                static_cast<Steinberg::Vst::IEventList*>(this);

            addRef();

            return Steinberg::kResultTrue;
        }

        if (Steinberg::FUnknownPrivate::iidEqual(
            iid,
            Steinberg::FUnknown::iid))
        {
            *obj =
                static_cast<Steinberg::FUnknown*>(
                    static_cast<Steinberg::Vst::IEventList*>(this));

            addRef();

            return Steinberg::kResultTrue;
        }

        return Steinberg::kNoInterface;
    }


    Steinberg::uint32 PLUGIN_API VSTEventList::addRef()
    {
        return ++_refCount;
    }


    Steinberg::uint32 PLUGIN_API VSTEventList::release()
    {
        const auto count = --_refCount;

        if (count == 0)
            delete this;

        return count;
    }


    int32_t PLUGIN_API VSTEventList::getEventCount()
    {
        return static_cast<int32_t>(_events.size());
    }


    Steinberg::tresult PLUGIN_API VSTEventList::getEvent(
        int32_t index,
        Steinberg::Vst::Event& e)
    {
        if (index < 0 ||
            index >= static_cast<int32_t>(_events.size()))
        {
            return Steinberg::kResultFalse;
        }

        e = _events[index];

        return Steinberg::kResultTrue;
    }


    Steinberg::tresult PLUGIN_API VSTEventList::addEvent(
        Steinberg::Vst::Event& e)
    {
        _events.push_back(e);

        return Steinberg::kResultTrue;
    }

    void VSTEventList::clear()
    {
        _events.clear();
        _data.clear();
    }


    void VSTEventList::swap(
        VSTEventList& other) noexcept
    {
        _events.swap(other._events);
        _data.swap(other._data);
    }

}
