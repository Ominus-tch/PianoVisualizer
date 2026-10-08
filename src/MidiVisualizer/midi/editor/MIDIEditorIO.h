#pragma once

#include <string>

#include "MIDIEditorDocument.h"

class MIDIEditorIO
{
public:
    static bool load(
        const std::string& filePath,
        MIDIEditorDocument& document,
        std::string& error
    );

    static bool save(
        const std::string& filePath,
        const MIDIEditorDocument& document,
        std::string& error
    );
};