#include "VSTPlugin.h"
#include "VSTAudio.h"
#include "VSTStateStream.h"

#include <Windows.h>

#include <fstream>

#include "../../../util/Logger.h"
#include "../../../util/Config.h"

#include "pluginterfaces/vst/vsttypes.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"


namespace vst
{
    static LRESULT CALLBACK vstEditorWindowProc(
        HWND hwnd,
        UINT msg,
        WPARAM wParam,
        LPARAM lParam)
    {
        auto* plugin =
            reinterpret_cast<VSTPlugin*>(
                GetWindowLongPtr(
                    hwnd,
                    GWLP_USERDATA));

        switch (msg)
        {
        case WM_SIZE:
        {
            if (plugin &&
                plugin->editor())
            {
                const int width =
                    LOWORD(lParam);

                const int height =
                    HIWORD(lParam);

                if (width > 0 &&
                    height > 0)
                {
                    Steinberg::ViewRect rect(
                        0,
                        0,
                        width,
                        height);

                    plugin->editor()->onSize(
                        &rect);
                }
            }

            break;
        }

        case WM_NCDESTROY:
        {
            if (plugin)
            {
                plugin->onEditorWindowDestroyed();
            }

            SetWindowLongPtr(
                hwnd,
                GWLP_USERDATA,
                0);

            break;
        }
        }

        return DefWindowProc(
            hwnd,
            msg,
            wParam,
            lParam);
    }


    VSTPlugin::VSTPlugin()
    {
        Logger::Log(
            "[VST] VSTPlugin created\n");
    }


    VSTPlugin::~VSTPlugin()
    {
        unload();
    }


    bool VSTPlugin::load(
        const std::string& path,
        double sampleRate)
    {
        unload();

        Logger::Log(
            "[VST] Loading plugin: %s\n",
            path.c_str());


        /*
         * --------------------------------------------------------
         * Module creation
         * --------------------------------------------------------
         */

        std::string error;

        Logger::Log(
            "[VST] Module::create START\n");

        _module =
            VST3::Hosting::Module::create(
                path,
                error);

        Logger::Log(
            "[VST] Module::create END module=%p\n",
            _module.get());

        if (!_module)
        {
            Logger::Log(
                "[VST] Failed to create module: %s\n",
                error.c_str());

            return false;
        }


        /*
         * --------------------------------------------------------
         * Create host interfaces
         * --------------------------------------------------------
         */

        Logger::Log(
            "[VST] Creating HostApplication START\n");

        _hostApplication =
            new VSTHostApplication();

        Logger::Log(
            "[VST] Creating HostApplication END ptr=%p\n",
            _hostApplication);

        Logger::Log(
            "[VST] Creating ComponentHandler START\n");

        _componentHandler =
            new VSTComponentHandler();

        Logger::Log(
            "[VST] Creating ComponentHandler END ptr=%p\n",
            _componentHandler);

        if (!_hostApplication ||
            !_componentHandler)
        {
            Logger::Log(
                "[VST] Failed to create host interfaces\n");

            unload();

            return false;
        }


        /*
         * --------------------------------------------------------
         * Factory
         * --------------------------------------------------------
         */

        Logger::Log(
            "[VST] getFactory START\n");

        const auto& factory =
            _module->getFactory();

        Logger::Log(
            "[VST] getFactory END\n");

        Logger::Log(
            "[VST] factory.classInfos START\n");

        const auto classes =
            factory.classInfos();

        Logger::Log(
            "[VST] factory.classInfos END count=%zu\n",
            classes.size());


        /*
         * --------------------------------------------------------
         * Find audio module
         * --------------------------------------------------------
         */

        for (const auto& classInfo : classes)
        {
            if (classInfo.category() !=
                "Audio Module Class")
            {
                continue;
            }

            _editorName =
                classInfo.name();


            /*
             * ----------------------------------------------------
             * Create component
             * ----------------------------------------------------
             */

            Logger::Log(
                "[VST] createInstance<IComponent> START: %s\n",
                classInfo.name().data());

            _component =
                factory.createInstance<
                Steinberg::Vst::IComponent>(
                    classInfo.ID());

            Logger::Log(
                "[VST] createInstance<IComponent> END component=%p\n",
                _component.get());

            if (!_component)
            {
                Logger::Log(
                    "[VST] Failed to create IComponent\n");

                unload();

                return false;
            }


            /*
             * ----------------------------------------------------
             * IPluginBase
             * ----------------------------------------------------
             */

            auto* componentPluginBase =
                static_cast<
                Steinberg::IPluginBase*>(
                    _component.get());

            if (!componentPluginBase)
            {
                Logger::Log(
                    "[VST] Failed to obtain IPluginBase from component\n");

                unload();

                return false;
            }


            /*
             * ----------------------------------------------------
             * Component initialize
             * ----------------------------------------------------
             */

            Logger::Log(
                "[VST] Component initialize START\n");

            auto result =
                componentPluginBase->initialize(
                    _hostApplication);

            Logger::Log(
                "[VST] Component initialize END result=%d\n",
                result);

            if (result !=
                Steinberg::kResultOk)
            {
                Logger::Log(
                    "[VST] Component initialize failed: result=%d\n",
                    result);

                unload();

                return false;
            }


            /*
             * ----------------------------------------------------
             * IAudioProcessor
             * ----------------------------------------------------
             */

            Steinberg::Vst::IAudioProcessor*
                processor = nullptr;

            Logger::Log(
                "[VST] queryInterface(IAudioProcessor) START\n");

            result =
                _component->queryInterface(
                    Steinberg::Vst::IAudioProcessor::iid,
                    reinterpret_cast<void**>(
                        &processor));

            Logger::Log(
                "[VST] queryInterface(IAudioProcessor) END result=%d processor=%p\n",
                result,
                processor);

            if (result !=
                Steinberg::kResultOk ||
                !processor)
            {
                Logger::Log(
                    "[VST] Failed to obtain IAudioProcessor\n");

                unload();

                return false;
            }

            _processor =
                processor;


            /*
             * ----------------------------------------------------
             * Controller
             * ----------------------------------------------------
             */

            _singleComponent =
                false;

            Steinberg::Vst::IEditController*
                controllerFromComponent =
                nullptr;

            Logger::Log(
                "[VST] queryInterface(IEditController) START\n");

            result =
                _component->queryInterface(
                    Steinberg::Vst::IEditController::iid,
                    reinterpret_cast<void**>(
                        &controllerFromComponent));

            Logger::Log(
                "[VST] queryInterface(IEditController) END result=%d controller=%p\n",
                result,
                controllerFromComponent);


            if (result ==
                Steinberg::kResultOk &&
                controllerFromComponent)
            {
                /*
                 * The component itself is also the controller.
                 */

                _controller =
                    controllerFromComponent;

                _singleComponent =
                    true;
            }
            else
            {
                /*
                 * ------------------------------------------------
                 * Get controller class ID
                 * ------------------------------------------------
                 */

                Steinberg::TUID controllerCID{};

                Logger::Log(
                    "[VST] getControllerClassId START\n");

                result =
                    _component->getControllerClassId(
                        controllerCID);

                Logger::Log(
                    "[VST] getControllerClassId END result=%d\n",
                    result);

                if (result !=
                    Steinberg::kResultOk)
                {
                    Logger::Log(
                        "[VST] Failed to get controller class ID: result=%d\n",
                        result);

                    unload();

                    return false;
                }


                /*
                 * ------------------------------------------------
                 * Create controller
                 * ------------------------------------------------
                 */

                Logger::Log(
                    "[VST] createInstance<IEditController> START\n");

                _controller =
                    factory.createInstance<
                    Steinberg::Vst::IEditController>(
                        VST3::UID(
                            controllerCID));

                Logger::Log(
                    "[VST] createInstance<IEditController> END controller=%p\n",
                    _controller.get());

                if (!_controller)
                {
                    Logger::Log(
                        "[VST] Failed to create IEditController\n");

                    unload();

                    return false;
                }


                /*
                 * ------------------------------------------------
                 * Controller initialize
                 * ------------------------------------------------
                 */

                auto* controllerPluginBase =
                    static_cast<
                    Steinberg::IPluginBase*>(
                        _controller.get());

                if (!controllerPluginBase)
                {
                    Logger::Log(
                        "[VST] Failed to obtain IPluginBase from controller\n");

                    unload();

                    return false;
                }

                Logger::Log(
                    "[VST] Controller initialize START\n");

                result =
                    controllerPluginBase->initialize(
                        _hostApplication);

                Logger::Log(
                    "[VST] Controller initialize END result=%d\n",
                    result);

                if (result !=
                    Steinberg::kResultOk)
                {
                    Logger::Log(
                        "[VST] Controller initialize failed: result=%d\n",
                        result);

                    unload();

                    return false;
                }
            }


            /*
             * ----------------------------------------------------
             * ComponentHandler
             * ----------------------------------------------------
             */

            if (_controller)
            {
                Logger::Log(
                    "[VST] setComponentHandler START\n");

                result =
                    _controller->setComponentHandler(
                        _componentHandler);

                Logger::Log(
                    "[VST] setComponentHandler END result=%d\n",
                    result);

                if (result !=
                    Steinberg::kResultOk)
                {
                    Logger::Log(
                        "[VST] setComponentHandler failed: result=%d\n",
                        result);

                    unload();

                    return false;
                }
            }


            /*
             * ----------------------------------------------------
             * Connection points
             * ----------------------------------------------------
             */

            if (!_singleComponent)
            {
                Steinberg::Vst::IConnectionPoint*
                    componentConnection =
                    nullptr;

                Steinberg::Vst::IConnectionPoint*
                    controllerConnection =
                    nullptr;


                Logger::Log(
                    "[VST] queryInterface(IConnectionPoint component) START\n");

                result =
                    _component->queryInterface(
                        Steinberg::Vst::IConnectionPoint::iid,
                        reinterpret_cast<void**>(
                            &componentConnection));

                Logger::Log(
                    "[VST] queryInterface(IConnectionPoint component) END result=%d ptr=%p\n",
                    result,
                    componentConnection);

                if (result !=
                    Steinberg::kResultOk ||
                    !componentConnection)
                {
                    Logger::Log(
                        "[VST] Component does not provide IConnectionPoint\n");

                    unload();

                    return false;
                }


                Logger::Log(
                    "[VST] queryInterface(IConnectionPoint controller) START\n");

                result =
                    _controller->queryInterface(
                        Steinberg::Vst::IConnectionPoint::iid,
                        reinterpret_cast<void**>(
                            &controllerConnection));

                Logger::Log(
                    "[VST] queryInterface(IConnectionPoint controller) END result=%d ptr=%p\n",
                    result,
                    controllerConnection);

                if (result !=
                    Steinberg::kResultOk ||
                    !controllerConnection)
                {
                    Logger::Log(
                        "[VST] Controller does not provide IConnectionPoint\n");

                    componentConnection->release();

                    unload();

                    return false;
                }


                /*
                 * queryInterface() gives us an owned reference.
                 */

                _componentConnection =
                    Steinberg::IPtr<
                    Steinberg::Vst::IConnectionPoint>(
                        componentConnection);

                _controllerConnection =
                    Steinberg::IPtr<
                    Steinberg::Vst::IConnectionPoint>(
                        controllerConnection);


                /*
                 * Component -> controller
                 */

                Logger::Log(
                    "[VST] componentConnection->connect START\n");

                result =
                    _componentConnection->connect(
                        _controllerConnection);

                Logger::Log(
                    "[VST] componentConnection->connect END result=%d\n",
                    result);

                if (result !=
                    Steinberg::kResultOk)
                {
                    Logger::Log(
                        "[VST] Failed to connect component to controller: result=%d\n",
                        result);

                    unload();

                    return false;
                }


                /*
                 * Controller -> component
                 */

                Logger::Log(
                    "[VST] controllerConnection->connect START\n");

                result =
                    _controllerConnection->connect(
                        _componentConnection);

                Logger::Log(
                    "[VST] controllerConnection->connect END result=%d\n",
                    result);

                if (result !=
                    Steinberg::kResultOk)
                {
                    Logger::Log(
                        "[VST] Failed to connect controller to component: result=%d\n",
                        result);

                    unload();

                    return false;
                }
            }


            /*
             * ----------------------------------------------------
             * Processing setup
             * ----------------------------------------------------
             */

            Steinberg::Vst::ProcessSetup setup{};

            setup.processMode =
                Steinberg::Vst::kRealtime;

            setup.symbolicSampleSize =
                Steinberg::Vst::kSample32;

            setup.sampleRate =
                sampleRate;

            setup.maxSamplesPerBlock =
                512;

            Logger::Log(
                "[VST] setupProcessing START sampleRate=%f maxBlockSize=%d\n",
                setup.sampleRate,
                setup.maxSamplesPerBlock);

            result =
                _processor->setupProcessing(
                    setup);

            Logger::Log(
                "[VST] setupProcessing END result=%d\n",
                result);

            if (result !=
                Steinberg::kResultOk)
            {
                Logger::Log(
                    "[VST] setupProcessing failed: result=%d\n",
                    result);

                unload();

                return false;
            }


            /*
             * ----------------------------------------------------
             * Activate output bus
             * ----------------------------------------------------
             */

            Logger::Log(
                "[VST] activateBus(output) START\n");

            result =
                _component->activateBus(
                    Steinberg::Vst::kAudio,
                    Steinberg::Vst::kOutput,
                    0,
                    true);

            Logger::Log(
                "[VST] activateBus(output) END result=%d\n",
                result);

            if (result !=
                Steinberg::kResultOk)
            {
                Logger::Log(
                    "[VST] Failed to activate output bus\n");

                unload();

                return false;
            }

            /*
             * --------------------------------------------------------
             * Load Plugin State
             * --------------------------------------------------------
             */

            const std::string statePath =
                Config::GetVSTStatePath(_editorName).string();

            loadState(statePath);

            /*
             * ----------------------------------------------------
             * Activate component
             * ----------------------------------------------------
             */

            Logger::Log(
                "[VST] setActive(true) START\n");

            result =
                _component->setActive(
                    true);

            Logger::Log(
                "[VST] setActive(true) END result=%d\n",
                result);

            if (result !=
                Steinberg::kResultOk)
            {
                Logger::Log(
                    "[VST] Failed to activate component: result=%d\n",
                    result);

                unload();

                return false;
            }


            /*
             * ----------------------------------------------------
             * Start processing
             * ----------------------------------------------------
             */

            Logger::Log(
                "[VST] setProcessing(true) START\n");

            result =
                _processor->setProcessing(
                    true);

            Logger::Log(
                "[VST] setProcessing(true) END result=%d\n",
                result);

            if (result !=
                Steinberg::kResultOk)
            {
                Logger::Log(
                    "[VST] Failed to start processing: result=%d\n",
                    result);

                unload();

                return false;
            }


            /*
             * ----------------------------------------------------
             * Success
             * ----------------------------------------------------
             */

            _path =
                path;

            Logger::Log(
                "[VST] Plugin initialized successfully: %s\n",
                classInfo.name().data());

            _loaded =
                true;

            return true;
        }


        /*
         * --------------------------------------------------------
         * No audio module
         * --------------------------------------------------------
         */

        Logger::Log(
            "[VST] No VST3 audio effect found in module\n");

        unload();

        return false;
    }


    void VSTPlugin::unload()
    {
        if (!_loaded &&
            !_module &&
            !_component &&
            !_controller &&
            !_processor &&
            !_editor &&
            !_editorWindow)
        {
            return;
        }


        /*
         * --------------------------------------------------------
         * Destroy editor
         * --------------------------------------------------------
         */

        if (_editor ||
            _editorWindow)
        {
            destroyEditor();
        }

        _loaded =
            false;

        Logger::Log(
            "[VST] Unloading plugin\n");


        /*
         * --------------------------------------------------------
         * Stop processing
         * --------------------------------------------------------
         */

        if (_processor)
        {
            Logger::Log(
                "[VST] setProcessing(false) START\n");

            auto result =
                _processor->setProcessing(false);

            Logger::Log(
                "[VST] setProcessing(false) END result=%d\n",
                result);
        }

        /*
         * --------------------------------------------------------
         * Save Plugin State
         * --------------------------------------------------------
         */

        const std::string statePath =
            Config::GetVSTStatePath(_editorName).string();

        saveState(statePath);

        /*
         * --------------------------------------------------------
         * Deactivate component
         * --------------------------------------------------------
         */

        if (_component)
        {
            Logger::Log(
                "[VST] setActive(false) START\n");

            auto result =
                _component->setActive(false);

            Logger::Log(
                "[VST] setActive(false) END result=%d\n",
                result);
        }


        /*
         * --------------------------------------------------------
         * Disconnect component/controller
         * --------------------------------------------------------
         */

        if (_componentConnection &&
            _controllerConnection)
        {
            Logger::Log(
                "[VST] componentConnection->disconnect START\n");

            auto componentResult =
                _componentConnection->disconnect(
                    _controllerConnection);

            Logger::Log(
                "[VST] componentConnection->disconnect END result=%d\n",
                componentResult);


            Logger::Log(
                "[VST] controllerConnection->disconnect START\n");

            auto controllerResult =
                _controllerConnection->disconnect(
                    _componentConnection);

            Logger::Log(
                "[VST] controllerConnection->disconnect END result=%d\n",
                controllerResult);
        }


        /*
         * Release connection points.
         */

        Logger::Log(
            "[VST] Releasing connection points\n");

        _componentConnection =
            nullptr;

        _controllerConnection =
            nullptr;


        /*
         * --------------------------------------------------------
         * Terminate component
         * --------------------------------------------------------
         */

        if (_component)
        {
            Logger::Log(
                "[VST] Component terminate START\n");

            auto* componentPluginBase =
                static_cast<
                Steinberg::IPluginBase*>(
                    _component.get());

            if (componentPluginBase)
            {
                auto result =
                    componentPluginBase->terminate();

                Logger::Log(
                    "[VST] Component terminate END result=%d\n",
                    result);
            }
            else
            {
                Logger::Log(
                    "[VST] Component IPluginBase is null\n");
            }
        }


        /*
         * --------------------------------------------------------
         * Terminate controller
         * --------------------------------------------------------
         */

        if (_controller &&
            !_singleComponent)
        {
            Logger::Log(
                "[VST] Controller terminate START\n");

            auto* controllerPluginBase =
                static_cast<
                Steinberg::IPluginBase*>(
                    _controller.get());

            if (controllerPluginBase)
            {
                auto result =
                    controllerPluginBase->terminate();

                Logger::Log(
                    "[VST] Controller terminate END result=%d\n",
                    result);
            }
            else
            {
                Logger::Log(
                    "[VST] Controller IPluginBase is null\n");
            }
        }


        /*
         * --------------------------------------------------------
         * Release plugin interfaces
         * --------------------------------------------------------
         */

        Logger::Log(
            "[VST] Releasing plugin interfaces\n");

        _processor =
            nullptr;

        _controller =
            nullptr;

        _component =
            nullptr;

        _singleComponent =
            false;


        /*
         * --------------------------------------------------------
         * Release host interfaces
         * --------------------------------------------------------
         */

        if (_componentHandler)
        {
            Logger::Log(
                "[VST] Destroying ComponentHandler\n");

            _componentHandler->release();

            _componentHandler =
                nullptr;
        }

        if (_hostApplication)
        {
            Logger::Log(
                "[VST] Destroying HostApplication\n");

            _hostApplication->release();

            _hostApplication =
                nullptr;
        }


        /*
         * --------------------------------------------------------
         * Finally unload module
         * --------------------------------------------------------
         */

        if (_module)
        {
            Logger::Log(
                "[VST] Unloading module\n");

            _module.reset();

            Logger::Log(
                "[VST] Module unloaded\n");
        }

        _editorName =
            "VST Editor";

        _path.clear();

        Logger::Log(
            "[VST] Plugin unload complete\n");
    }


    bool VSTPlugin::isLoaded() const
    {
        return _module != nullptr &&
            _component != nullptr;
    }


    const std::string& VSTPlugin::path() const
    {
        return _path;
    }

    bool VSTPlugin::saveState(
        const std::string& path)
    {
        if (!_component)
        {
            Logger::Log(
                "[VST] Cannot save state: component is null\n");

            return false;
        }

        Logger::Log(
            "[VST] Saving plugin state: %s\n",
            path.c_str());

        VSTStateStream componentStream;

        auto result =
            _component->getState(
                &componentStream);

        if (result != Steinberg::kResultOk)
        {
            Logger::Log(
                "[VST] Component getState failed: result=%d\n",
                result);

            return false;
        }

        const auto& componentData =
            componentStream.data();

        std::vector<uint8_t> controllerData;

        if (_controller)
        {
            VSTStateStream controllerStream;

            result =
                _controller->getState(
                    &controllerStream);

            if (result == Steinberg::kResultOk)
            {
                controllerData =
                    controllerStream.data();
            }
            else
            {
                Logger::Log(
                    "[VST] Controller getState unsupported: result=%d\n",
                    result);
            }
        }

        std::ofstream file(
            path,
            std::ios::binary |
            std::ios::trunc);

        if (!file)
        {
            Logger::Log(
                "[VST] Failed to open state file for writing: %s\n",
                path.c_str());

            return false;
        }

        const uint32_t version = 1;

        const uint64_t componentSize =
            static_cast<uint64_t>(
                componentData.size());

        const uint64_t controllerSize =
            static_cast<uint64_t>(
                controllerData.size());

        file.write(
            reinterpret_cast<const char*>(&version),
            sizeof(version));

        file.write(
            reinterpret_cast<const char*>(&componentSize),
            sizeof(componentSize));

        if (!componentData.empty())
        {
            file.write(
                reinterpret_cast<const char*>(
                    componentData.data()),
                componentData.size());
        }

        file.write(
            reinterpret_cast<const char*>(&controllerSize),
            sizeof(controllerSize));

        if (!controllerData.empty())
        {
            file.write(
                reinterpret_cast<const char*>(
                    controllerData.data()),
                controllerData.size());
        }

        if (!file.good())
        {
            Logger::Log(
                "[VST] Failed while writing state file\n");

            return false;
        }

        Logger::Log(
            "[VST] Plugin state saved successfully "
            "(component=%llu bytes, controller=%llu bytes)\n",
            static_cast<unsigned long long>(
                componentSize),
            static_cast<unsigned long long>(
                controllerSize));

        return true;
    }

    bool VSTPlugin::loadState(
        const std::string& path)
    {
        if (!_component)
        {
            Logger::Log(
                "[VST] Cannot load state: component is null\n");

            return false;
        }

        std::ifstream file(
            path,
            std::ios::binary |
            std::ios::ate);

        if (!file)
        {
            Logger::Log(
                "[VST] State file does not exist: %s\n",
                path.c_str());

            return false;
        }

        const std::streamsize fileSize =
            file.tellg();

        if (fileSize < 0)
        {
            Logger::Log(
                "[VST] Failed to determine state file size\n");

            return false;
        }

        file.seekg(0);

        std::vector<uint8_t> fileData(
            static_cast<size_t>(fileSize));

        if (!fileData.empty())
        {
            file.read(
                reinterpret_cast<char*>(
                    fileData.data()),
                fileSize);
        }

        if (!file.good() &&
            !file.eof())
        {
            Logger::Log(
                "[VST] Failed to read state file\n");

            return false;
        }

        size_t offset = 0;

        auto readBytes =
            [&](void* destination,
                size_t size) -> bool
            {
                if (offset + size > fileData.size())
                    return false;

                std::memcpy(
                    destination,
                    fileData.data() + offset,
                    size);

                offset += size;

                return true;
            };

        uint32_t version = 0;

        if (!readBytes(
            &version,
            sizeof(version)))
        {
            Logger::Log(
                "[VST] Invalid state file: missing version\n");

            return false;
        }

        if (version != 1)
        {
            Logger::Log(
                "[VST] Unsupported state file version: %u\n",
                version);

            return false;
        }

        uint64_t componentSize = 0;

        if (!readBytes(
            &componentSize,
            sizeof(componentSize)))
        {
            Logger::Log(
                "[VST] Invalid state file: missing component size\n");

            return false;
        }

        if (componentSize >
            fileData.size() - offset)
        {
            Logger::Log(
                "[VST] Invalid component state size\n");

            return false;
        }

        std::vector<uint8_t> componentData(
            fileData.begin() + offset,
            fileData.begin() +
            offset +
            static_cast<size_t>(
                componentSize));

        offset +=
            static_cast<size_t>(
                componentSize);

        uint64_t controllerSize = 0;

        if (!readBytes(
            &controllerSize,
            sizeof(controllerSize)))
        {
            Logger::Log(
                "[VST] Invalid state file: missing controller size\n");

            return false;
        }

        if (controllerSize >
            fileData.size() - offset)
        {
            Logger::Log(
                "[VST] Invalid controller state size\n");

            return false;
        }

        std::vector<uint8_t> controllerData(
            fileData.begin() + offset,
            fileData.begin() +
            offset +
            static_cast<size_t>(
                controllerSize));

        Logger::Log(
            "[VST] Loading plugin state: %s "
            "(component=%llu bytes, controller=%llu bytes)\n",
            path.c_str(),
            static_cast<unsigned long long>(
                componentSize),
            static_cast<unsigned long long>(
                controllerSize));

        VSTStateStream componentStream(
            componentData);

        auto result =
            _component->setState(
                &componentStream);

        if (result != Steinberg::kResultOk)
        {
            Logger::Log(
                "[VST] Component setState failed: result=%d\n",
                result);

            return false;
        }

        if (controllerSize > 0)
        {
            VSTStateStream controllerStream(
                controllerData);

            result =
                _controller->setState(
                    &controllerStream);

            if (result != Steinberg::kResultOk)
            {
                Logger::Log(
                    "[VST] Controller setState failed: result=%d\n",
                    result);

                return false;
            }
        }

        Logger::Log(
            "[VST] Plugin state loaded successfully\n");

        return true;
    }

    Steinberg::Vst::IComponent*
        VSTPlugin::component() const
    {
        return _component;
    }


    Steinberg::Vst::IEditController*
        VSTPlugin::controller() const
    {
        return _controller;
    }


    Steinberg::Vst::IAudioProcessor*
        VSTPlugin::processor() const
    {
        return _processor;
    }


    bool VSTPlugin::createEditor(
        HINSTANCE instance)
    {
        Logger::Log(
            "[VST] Create Editor START\n");

        if (_editor)
        {
            Logger::Log(
                "[VST] Editor already exists\n");

            if (_editorWindow)
            {
                ShowWindow(
                    _editorWindow,
                    SW_SHOW);

                SetForegroundWindow(
                    _editorWindow);
            }

            return true;
        }

        if (!_controller)
        {
            Logger::Log(
                "[VST] Cannot create editor: controller is null\n");

            return false;
        }


        /*
         * --------------------------------------------------------
         * Create plugin editor
         * --------------------------------------------------------
         */

        Logger::Log(
            "[VST] controller->createView START\n");

        Steinberg::IPlugView* view =
            _controller->createView(
                Steinberg::Vst::ViewType::kEditor);

        Logger::Log(
            "[VST] controller->createView END view=%p\n",
            view);

        if (!view)
        {
            Logger::Log(
                "[VST] Plugin did not provide an editor\n");

            return false;
        }

        _editor =
            Steinberg::IPtr<
            Steinberg::IPlugView>(
                view);

        if (!_editor)
        {
            Logger::Log(
                "[VST] Failed to store editor\n");

            return false;
        }


        /*
         * --------------------------------------------------------
         * Platform support
         * --------------------------------------------------------
         */

        auto platformResult =
            _editor->isPlatformTypeSupported(
                Steinberg::kPlatformTypeHWND);

        if (platformResult !=
            Steinberg::kResultTrue)
        {
            Logger::Log(
                "[VST] Editor does not support HWND\n");

            _editor =
                nullptr;

            return false;
        }


        /*
         * --------------------------------------------------------
         * Editor size
         * --------------------------------------------------------
         */

        Steinberg::ViewRect rect{};

        auto sizeResult =
            _editor->getSize(
                &rect);

        if (sizeResult !=
            Steinberg::kResultTrue)
        {
            Logger::Log(
                "[VST] Failed to get editor size\n");

            _editor =
                nullptr;

            return false;
        }

        const int width =
            rect.getWidth();

        const int height =
            rect.getHeight();


        /*
         * --------------------------------------------------------
         * Host window size
         * --------------------------------------------------------
         */

        const DWORD style =
            WS_OVERLAPPEDWINDOW;

        const DWORD exStyle =
            0;

        RECT windowRect{
            0,
            0,
            width,
            height
        };

        if (!AdjustWindowRectEx(
            &windowRect,
            style,
            FALSE,
            exStyle))
        {
            Logger::Log(
                "[VST] Failed to calculate editor window size\n");

            _editor =
                nullptr;

            return false;
        }

        const int windowWidth =
            windowRect.right -
            windowRect.left;

        const int windowHeight =
            windowRect.bottom -
            windowRect.top;


        /*
         * --------------------------------------------------------
         * Register editor host window class
         * --------------------------------------------------------
         */

        static bool windowClassRegistered =
            false;

        if (!windowClassRegistered)
        {

            WNDCLASSA wc{};

            wc.style =
                CS_HREDRAW |
                CS_VREDRAW;

            wc.lpfnWndProc =
                vstEditorWindowProc;

            wc.hInstance =
                instance;

            wc.hCursor =
                LoadCursor(
                    nullptr,
                    IDC_ARROW);

            wc.lpszClassName =
                "VST Editor Host";

            if (!RegisterClassA(
                &wc))
            {
                if (GetLastError() !=
                    ERROR_CLASS_ALREADY_EXISTS)
                {
                    Logger::Log(
                        "[VST] Failed to register editor window class\n");

                    _editor =
                        nullptr;

                    return false;
                }
            }

            windowClassRegistered =
                true;
        }


        /*
         * --------------------------------------------------------
         * Create host window
         * --------------------------------------------------------
         */

        _editorWindow =
            CreateWindowExA(
                exStyle,
                "VST Editor Host",
                _editorName.c_str(),
                style,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                windowWidth,
                windowHeight,
                nullptr,
                nullptr,
                instance,
                nullptr);

        if (!_editorWindow)
        {
            Logger::Log(
                "[VST] Failed to create editor window\n");

            _editor =
                nullptr;

            return false;
        }


        SetWindowLongPtr(
            _editorWindow,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(
                this));


        /*
         * --------------------------------------------------------
         * Attach editor
         * --------------------------------------------------------
         */

        const auto result =
            _editor->attached(
                _editorWindow,
                Steinberg::kPlatformTypeHWND);

        if (result !=
            Steinberg::kResultOk)
        {
            Logger::Log(
                "[VST] Failed to attach editor: result=%d\n",
                result);

            SetWindowLongPtr(
                _editorWindow,
                GWLP_USERDATA,
                0);

            DestroyWindow(
                _editorWindow);

            _editorWindow =
                nullptr;

            _editor =
                nullptr;

            return false;
        }


        /*
         * --------------------------------------------------------
         * Show editor
         * --------------------------------------------------------
         */

        ShowWindow(
            _editorWindow,
            SW_SHOW);

        UpdateWindow(
            _editorWindow);

        Logger::Log(
            "[VST] Editor attached successfully\n");

        return true;
    }


    void VSTPlugin::destroyEditor()
    {
        if (!_editor &&
            !_editorWindow)
        {
            return;
        }

        _destroyingEditor =
            true;

        if (_editor)
        {

            _editor->removed();

            _editor =
                nullptr;
        }

        if (_editorWindow)
        {
            SetWindowLongPtr(
                _editorWindow,
                GWLP_USERDATA,
                0);

            DestroyWindow(
                _editorWindow);

            _editorWindow =
                nullptr;
        }

        _destroyingEditor =
            false;

        Logger::Log(
            "[VST] Editor destroyed\n");
    }


    bool VSTPlugin::hasEditor() const
    {
        return _editor != nullptr &&
            _editorWindow != nullptr;
    }


    HWND VSTPlugin::editorWindow() const
    {
        return _editorWindow;
    }


    Steinberg::IPlugView*
        VSTPlugin::editor() const
    {
        return _editor;
    }


    void VSTPlugin::resizeEditor(
        int x,
        int y,
        int width,
        int height)
    {
        if (!_editor ||
            !_editorWindow)
        {
            return;
        }

        DWORD style =
            static_cast<DWORD>(
                GetWindowLongPtr(
                    _editorWindow,
                    GWL_STYLE));

        DWORD exStyle =
            static_cast<DWORD>(
                GetWindowLongPtr(
                    _editorWindow,
                    GWL_EXSTYLE));

        RECT rect{
            0,
            0,
            width,
            height
        };

        if (!AdjustWindowRectEx(
            &rect,
            style,
            FALSE,
            exStyle))
        {
            return;
        }

        const int windowWidth =
            rect.right -
            rect.left;

        const int windowHeight =
            rect.bottom -
            rect.top;

        SetWindowPos(
            _editorWindow,
            nullptr,
            x,
            y,
            windowWidth,
            windowHeight,
            SWP_NOACTIVATE |
            SWP_NOZORDER);
    }


    void VSTPlugin::onEditorWindowDestroyed()
    {
        if (!_editorWindow)
            return;

        Logger::Log(
            "[VST] Editor window destroyed\n");

        _editorWindow =
            nullptr;

        if (_editor)
        {

            _editor->removed();

            _editor =
                nullptr;
        }

        if (!_destroyingEditor)
        {
            Logger::Log(
                "[VST] Editor closed by user\n");
        }
    }
}