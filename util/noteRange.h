
#pragma once

#include <charconv>
#include <cctype>
#include <string_view>
#include <cmath>

namespace PianoNoteRange
{
    constexpr int FIRST_MIDI_NOTE = 21; // A0
    constexpr int LAST_MIDI_NOTE = 108; // C8

    constexpr float BLACK_KEY_WIDTH = 0.60f;
    constexpr float WHITE_KEY_COUNT = 52.0f;

    constexpr float FULL_PIANO_WIDTH = 16.0f / 9.0f;

    inline bool IsWhiteKey(int midi)
    {
        switch (midi % 12)
        {
        case 0:  // C
        case 2:  // D
        case 4:  // E
        case 5:  // F
        case 7:  // G
        case 9:  // A
        case 11: // B
            return true;
        default:
            return false;
        }
    }

    inline int CountWhiteKeysBefore(int midi)
    {
        int count = 0;

        for (int n = FIRST_MIDI_NOTE; n < midi; ++n)
        {
            if (IsWhiteKey(n))
                ++count;
        }

        return count;
    }

    // Returns the physical left and right edges of a key,
    // measured in white-key widths from the left edge of A0.
    inline bool GetKeyBounds(
        int midi,
        float& left,
        float& right
    )
    {
        if (midi < FIRST_MIDI_NOTE ||
            midi > LAST_MIDI_NOTE)
        {
            return false;
        }

        const float whiteIndex =
            static_cast<float>(
                CountWhiteKeysBefore(midi)
                );

        if (IsWhiteKey(midi))
        {
            left = whiteIndex;
            right = whiteIndex + 1.0f;
        }
        else
        {
            const float halfWidth =
                BLACK_KEY_WIDTH * 0.5f;

            left = whiteIndex - halfWidth;
            right = whiteIndex + halfWidth;
        }

        return true;
    }

    inline bool TryParseNote(
        std::string_view text,
        int& outMidi
    )
    {
        if (text.size() < 2)
            return false;

        const char letter =
            static_cast<char>(
                std::toupper(
                    static_cast<unsigned char>(text[0])
                )
                );

        int pitchClass = -1;

        switch (letter)
        {
        case 'C': pitchClass = 0;  break;
        case 'D': pitchClass = 2;  break;
        case 'E': pitchClass = 4;  break;
        case 'F': pitchClass = 5;  break;
        case 'G': pitchClass = 7;  break;
        case 'A': pitchClass = 9;  break;
        case 'B': pitchClass = 11; break;
        default: return false;
        }

        size_t octaveStart = 1;

        int accidental = 0;

        if (text[octaveStart] == '#')
        {
            accidental = 1;
            ++octaveStart;
        }
        else if (text[octaveStart] == 'b')
        {
            accidental = -1;
            ++octaveStart;
        }

        if (octaveStart >= text.size())
            return false;

        int octave = 0;

        const char* begin = text.data() + octaveStart;
        const char* end = text.data() + text.size();

        auto result = std::from_chars(begin, end, octave);

        if (result.ec != std::errc{} || result.ptr != end)
            return false;

        const int midi =
            (octave + 1) * 12 +
            pitchClass +
            accidental;

        if (midi < FIRST_MIDI_NOTE ||
            midi > LAST_MIDI_NOTE)
        {
            return false;
        }

        outMidi = midi;
        return true;
    }

    // Converts an interval into normalized coordinates
    // across the entire 88-key keyboard.
    //
    // The interval boundaries are the OUTER EDGES of
    // the first and last notes, including black keys.
    inline bool GetIntervalFractions(
        int firstMidi,
        int lastMidi,
        float& leftFraction,
        float& rightFraction
    )
    {
        if (firstMidi >= lastMidi)
            return false;

        float leftKey;
        float unusedRight;
        float unusedLeft;
        float rightKey;

        if (!GetKeyBounds(firstMidi, leftKey, unusedRight))
            return false;

        if (!GetKeyBounds(lastMidi, unusedLeft, rightKey))
            return false;

        leftFraction =
            leftKey / WHITE_KEY_COUNT;

        rightFraction =
            rightKey / WHITE_KEY_COUNT;

        return
            rightFraction > leftFraction &&
            std::isfinite(leftFraction) &&
            std::isfinite(rightFraction);
    }
}
