#include "FileDropTarget.h"

#include "../../util/Logger.h"

#include <shellapi.h>

FileDropTarget::FileDropTarget(
    std::vector<std::wstring> extensions)
    :
    _extensions(
        std::move(extensions))
{
    /*
     * Normalize extensions so comparisons are
     * case-insensitive and always start with '.'.
     */
    for (auto& extension : _extensions)
    {
        if (!extension.empty() &&
            extension.front() != L'.')
        {
            extension.insert(
                extension.begin(),
                L'.');
        }

        for (auto& character : extension)
        {
            character =
                static_cast<wchar_t>(
                    towlower(character));
        }
    }
}


bool FileDropTarget::Register(
    HWND window)
{
    if (_registered)
        return true;

    if (!window)
    {
        Logger::Log(
            "[DragDrop] Cannot register: window is null\n");

        return false;
    }

    const HRESULT result =
        RegisterDragDrop(
            window,
            this);

    if (FAILED(result))
    {
        Logger::Log(
            "[DragDrop] RegisterDragDrop failed: HRESULT=0x%08X\n",
            static_cast<unsigned int>(result));

        return false;
    }

    _window = window;
    _registered = true;

    Logger::Log(
        "[DragDrop] File drop target registered\n\n");

    return true;
}


void FileDropTarget::Unregister()
{
    if (!_registered)
        return;

    RevokeDragDrop(_window);

    _window = nullptr;
    _registered = false;

    _dragging = false;
    _hasValidFile = false;
    _droppedFiles.clear();

    Logger::Log(
        "[DragDrop] File drop target unregistered\n");
}


bool FileDropTarget::isDragging() const
{
    return _dragging;
}


bool FileDropTarget::hasValidFile() const
{
    return _hasValidFile;
}

std::vector<std::filesystem::path> FileDropTarget::consumeDroppedFiles()
{
    return std::exchange(_droppedFiles, {});
}


HRESULT STDMETHODCALLTYPE
FileDropTarget::QueryInterface(
    REFIID riid,
    void** ppvObject)
{
    if (!ppvObject)
        return E_POINTER;

    *ppvObject = nullptr;

    if (riid == IID_IUnknown ||
        riid == IID_IDropTarget)
    {
        *ppvObject =
            static_cast<IDropTarget*>(
                this);

        AddRef();

        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE
FileDropTarget::AddRef()
{
    /*
     * The FileDropTarget is owned by the application
     * and lives until after RevokeDragDrop().
     *
     * RegisterDragDrop does not require us to dynamically
     * allocate the object, so a real reference count is
     * unnecessary here.
     */
    return 1;
}


ULONG STDMETHODCALLTYPE
FileDropTarget::Release()
{
    return 1;
}


HRESULT STDMETHODCALLTYPE
FileDropTarget::DragEnter(
    IDataObject* dataObject,
    DWORD,
    POINTL,
    DWORD* effect)
{
    if (!effect)
        return E_POINTER;

    _dragging = true;

    updateDragState(dataObject);

    if (_hasValidFile)
    {
        *effect = DROPEFFECT_COPY;

        Logger::Log(
            "[DragDrop] Valid file entered drop area\n");
    }
    else
    {
        *effect = DROPEFFECT_NONE;

        Logger::Log(
            "[DragDrop] Invalid file entered drop area\n");
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE
FileDropTarget::DragOver(
    DWORD,
    POINTL,
    DWORD* effect)
{
    if (!effect)
        return E_POINTER;

    if (_hasValidFile)
        *effect = DROPEFFECT_COPY;
    else
        *effect = DROPEFFECT_NONE;

    return S_OK;
}


HRESULT STDMETHODCALLTYPE
FileDropTarget::DragLeave()
{
    _dragging = false;
    _hasValidFile = false;

    Logger::Log(
        "[DragDrop] Drag left drop area\n");

    return S_OK;
}


HRESULT STDMETHODCALLTYPE
FileDropTarget::Drop(
    IDataObject* dataObject,
    DWORD,
    POINTL,
    DWORD* effect)
{
    if (!effect)
        return E_POINTER;

    _dragging = false;
    _hasValidFile = false;
    _droppedFiles.clear();

    std::vector<std::filesystem::path> files;

    if (!extractFiles(
        dataObject,
        files))
    {
        *effect = DROPEFFECT_NONE;

        Logger::Log(
            "[DragDrop] Failed to extract dropped files\n");

        return S_OK;
    }

    for (const auto& file : files)
    {
        if (!isValidFile(file))
            continue;

        _droppedFiles.push_back(file);

        Logger::Log(
            "[DragDrop] File dropped: %ls\n",
            file.c_str());
    }

    if (_droppedFiles.empty())
    {
        *effect = DROPEFFECT_NONE;

        Logger::Log(
            "[DragDrop] No valid files were dropped\n");

        return S_OK;
    }

    *effect = DROPEFFECT_COPY;

    Logger::Log(
        "[DragDrop] %zu valid file(s) dropped\n",
        _droppedFiles.size());

    return S_OK;
}

bool FileDropTarget::extractFiles(
    IDataObject* dataObject,
    std::vector<std::filesystem::path>& files) const
{
    files.clear();

    if (!dataObject)
        return false;

    FORMATETC format{
        CF_HDROP,
        nullptr,
        DVASPECT_CONTENT,
        -1,
        TYMED_HGLOBAL
    };

    STGMEDIUM medium{};

    const HRESULT result =
        dataObject->GetData(
            &format,
            &medium);

    if (FAILED(result))
        return false;

    HDROP drop =
        static_cast<HDROP>(
            medium.hGlobal);

    const UINT fileCount =
        DragQueryFileW(
            drop,
            0xFFFFFFFF,
            nullptr,
            0);

    for (UINT i = 0;
        i < fileCount;
        ++i)
    {
        const UINT requiredSize =
            DragQueryFileW(
                drop,
                i,
                nullptr,
                0);

        if (requiredSize == 0)
            continue;

        std::wstring path(
            requiredSize + 1,
            L'\0');

        const UINT copied =
            DragQueryFileW(
                drop,
                i,
                path.data(),
                static_cast<UINT>(
                    path.size()));

        if (copied == 0)
            continue;

        path.resize(copied);

        files.emplace_back(path);
    }

    ReleaseStgMedium(&medium);

    return true;
}

bool FileDropTarget::isValidFile(
    const std::filesystem::path& path) const
{
    if (_extensions.empty())
        return true;

    const bool isFile =
        std::filesystem::is_regular_file(path);

    const bool isDirectory =
        std::filesystem::is_directory(path);

    if (!isFile && !isDirectory)
        return false;

    std::wstring name =
        path.filename().wstring();

    for (auto& character : name)
    {
        character =
            static_cast<wchar_t>(
                towlower(character));
    }

    for (const auto& accepted :
        _extensions)
    {
        /*
         * Directories such as "LABS.vst3" are valid
         * when their name ends with the accepted suffix.
         */
        if (isDirectory)
        {
            if (name.size() >= accepted.size() &&
                name.compare(
                    name.size() - accepted.size(),
                    accepted.size(),
                    accepted) == 0)
            {
                return true;
            }
        }
        /*
         * Regular files continue to use their extension.
         */
        else
        {
            std::wstring extension =
                path.extension().wstring();

            for (auto& character : extension)
            {
                character =
                    static_cast<wchar_t>(
                        towlower(character));
            }

            if (extension == accepted)
                return true;
        }
    }

    return false;
}


void FileDropTarget::updateDragState(
    IDataObject* dataObject)
{
    std::vector<std::filesystem::path> files;

    if (!extractFiles(
        dataObject,
        files))
    {
        _hasValidFile = false;
        return;
    }

    _hasValidFile = false;

    for (const auto& file : files)
    {
        if (isValidFile(file))
        {
            _hasValidFile = true;
            break;
        }
    }
}