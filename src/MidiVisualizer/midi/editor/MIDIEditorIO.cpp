#include "MIDIEditorIO.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
    class ByteReader
    {
    public:
        explicit ByteReader(
            const std::vector<uint8_t>& bytes
        )
            : _bytes(bytes)
        {
        }

        size_t position() const
        {
            return _position;
        }

        bool seek(size_t position)
        {
            if (position > _bytes.size())
                return false;

            _position = position;
            return true;
        }

        size_t remaining() const
        {
            return _bytes.size() - _position;
        }

        bool canRead(size_t count) const
        {
            return count <= remaining();
        }

        bool read8(uint8_t& value)
        {
            if (!canRead(1))
                return false;

            value = _bytes[_position++];
            return true;
        }

        bool read16BE(uint16_t& value)
        {
            if (!canRead(2))
                return false;

            value =
                (static_cast<uint16_t>(
                    _bytes[_position]
                    ) << 8) |
                static_cast<uint16_t>(
                    _bytes[_position + 1]
                    );

            _position += 2;

            return true;
        }

        bool read32BE(uint32_t& value)
        {
            if (!canRead(4))
                return false;

            value =
                (static_cast<uint32_t>(
                    _bytes[_position]
                    ) << 24) |
                (static_cast<uint32_t>(
                    _bytes[_position + 1]
                    ) << 16) |
                (static_cast<uint32_t>(
                    _bytes[_position + 2]
                    ) << 8) |
                static_cast<uint32_t>(
                    _bytes[_position + 3]
                    );

            _position += 4;

            return true;
        }

        bool readBytes(
            size_t count,
            std::vector<uint8_t>& output
        )
        {
            if (!canRead(count))
                return false;

            output.assign(
                _bytes.begin() +
                static_cast<std::ptrdiff_t>(
                    _position
                    ),
                _bytes.begin() +
                static_cast<std::ptrdiff_t>(
                    _position + count
                    )
            );

            _position += count;

            return true;
        }

        bool skip(size_t count)
        {
            if (!canRead(count))
                return false;

            _position += count;

            return true;
        }

    private:
        const std::vector<uint8_t>& _bytes;

        size_t _position = 0;
    };

    bool readVariableLengthQuantity(
        ByteReader& reader,
        uint32_t& value
    )
    {
        value = 0;

        // MIDI VLQs are at most four bytes.
        for (int i = 0; i < 4; ++i)
        {
            uint8_t byte = 0;

            if (!reader.read8(byte))
                return false;

            value =
                (value << 7) |
                static_cast<uint32_t>(
                    byte & 0x7F
                    );

            if ((byte & 0x80) == 0)
                return true;
        }

        return false;
    }

    std::string makeError(
        size_t trackIndex,
        const char* message
    )
    {
        std::ostringstream stream;

        stream
            << "Track "
            << trackIndex
            << ": "
            << message;

        return stream.str();
    }

    struct PendingNote
    {
        uint64_t startTick = 0;
        uint8_t velocity = 0;
    };

    using PendingNoteTable =
        std::array<
        std::vector<PendingNote>,
        16 * 128
        >;

    size_t pendingIndex(
        uint8_t channel,
        uint8_t pitch
    )
    {
        return
            static_cast<size_t>(channel) * 128u +
            static_cast<size_t>(pitch);
    }

    void addExtractedNote(
        MIDIEditorDocument& document,
        MIDIEditorTrack& track,
        uint8_t channel,
        uint8_t pitch,
        const PendingNote& pending,
        uint64_t endTick
    )
    {
        MIDIEditorNote note;

        note.id =
            document.nextNoteId++;

        note.pitch =
            static_cast<int>(pitch);

        note.velocity =
            static_cast<int>(
                pending.velocity
                );

        note.channel =
            static_cast<int>(channel);

        note.startTick =
            pending.startTick;

        note.durationTick =
            endTick >= pending.startTick
            ? endTick - pending.startTick
            : 0;

        track.notes.push_back(note);
    }

    bool parseTrack(
        const std::vector<uint8_t>& trackBytes,
        size_t trackIndex,
        MIDIEditorDocument& document,
        MIDIEditorTrack& track,
        std::string& error
    )
    {
        ByteReader reader(trackBytes);

        PendingNoteTable pendingNotes;

        uint8_t runningStatus = 0;

        uint64_t absoluteTick = 0;

        bool reachedEndOfTrack = false;

        while (reader.remaining() > 0)
        {
            uint32_t delta = 0;

            if (!readVariableLengthQuantity(
                reader,
                delta
            ))
            {
                error = makeError(
                    trackIndex,
                    "invalid or truncated delta-time value"
                );

                return false;
            }

            absoluteTick +=
                static_cast<uint64_t>(
                    delta
                    );

            uint8_t firstByte = 0;

            if (!reader.read8(firstByte))
            {
                error = makeError(
                    trackIndex,
                    "missing event status byte"
                );

                return false;
            }

            // -----------------------------------------------------------------
            // META EVENT
            // -----------------------------------------------------------------

            if (firstByte == 0xFF)
            {
                runningStatus = 0;

                uint8_t metaType = 0;
                uint32_t length = 0;

                if (
                    !reader.read8(metaType) ||
                    !readVariableLengthQuantity(
                        reader,
                        length
                    )
                    )
                {
                    error = makeError(
                        trackIndex,
                        "truncated meta event"
                    );

                    return false;
                }

                std::vector<uint8_t> payload;

                if (!reader.readBytes(
                    length,
                    payload
                ))
                {
                    error = makeError(
                        trackIndex,
                        "meta event extends beyond track data"
                    );

                    return false;
                }

                MIDIEditorEvent event;

                event.tick =
                    absoluteTick;

                event.kind =
                    MIDIEditorEvent::Kind::META;

                event.status =
                    0xFF;

                event.type =
                    metaType;

                event.data =
                    payload;

                track.events.push_back(event);

                // Track name.
                if (metaType == 0x03)
                {
                    track.name.assign(
                        payload.begin(),
                        payload.end()
                    );
                }
                // Instrument name.
                else if (metaType == 0x04)
                {
                    track.instrument.assign(
                        payload.begin(),
                        payload.end()
                    );
                }
                // Tempo.
                else if (
                    metaType == 0x51 &&
                    payload.size() == 3
                    )
                {
                    const uint32_t microsecondsPerQuarterNote =
                        (static_cast<uint32_t>(
                            payload[0]
                            ) << 16) |
                        (static_cast<uint32_t>(
                            payload[1]
                            ) << 8) |
                        static_cast<uint32_t>(
                            payload[2]
                            );

                    document.tempos.push_back({
                        absoluteTick,
                        microsecondsPerQuarterNote
                        });
                }
                // Time signature.
                else if (
                    metaType == 0x58 &&
                    payload.size() >= 2
                    )
                {
                    MIDIEditorTimeSignature signature;

                    signature.tick =
                        absoluteTick;

                    signature.numerator =
                        payload[0];

                    signature.denominatorPower =
                        payload[1];

                    document.timeSignatures.push_back(
                        signature
                    );
                }
                // Key signature.
                else if (
                    metaType == 0x59 &&
                    payload.size() >= 2
                    )
                {
                    MIDIEditorKeySignature signature;

                    signature.tick =
                        absoluteTick;

                    signature.sharpsFlats =
                        static_cast<int8_t>(
                            payload[0]
                            );

                    signature.minor =
                        payload[1] != 0;

                    document.keySignatures.push_back(
                        signature
                    );
                }

                if (metaType == 0x2F)
                {
                    reachedEndOfTrack = true;
                    break;
                }

                continue;
            }

            // -----------------------------------------------------------------
            // SYSEX EVENT
            // -----------------------------------------------------------------

            if (
                firstByte == 0xF0 ||
                firstByte == 0xF7
                )
            {
                runningStatus = 0;

                uint32_t length = 0;

                if (!readVariableLengthQuantity(
                    reader,
                    length
                ))
                {
                    error = makeError(
                        trackIndex,
                        "truncated SysEx event length"
                    );

                    return false;
                }

                std::vector<uint8_t> payload;

                if (!reader.readBytes(
                    length,
                    payload
                ))
                {
                    error = makeError(
                        trackIndex,
                        "SysEx event extends beyond track data"
                    );

                    return false;
                }

                MIDIEditorEvent event;

                event.tick =
                    absoluteTick;

                event.kind =
                    MIDIEditorEvent::Kind::SYSEX;

                event.status =
                    firstByte;

                event.type =
                    0;

                event.data =
                    payload;

                track.events.push_back(event);

                continue;
            }

            // -----------------------------------------------------------------
            // MIDI CHANNEL EVENT
            // -----------------------------------------------------------------

            uint8_t status = firstByte;

            if ((status & 0x80) == 0)
            {
                if (
                    runningStatus < 0x80 ||
                    runningStatus >= 0xF0
                    )
                {
                    error = makeError(
                        trackIndex,
                        "running status used without a valid previous MIDI status"
                    );

                    return false;
                }

                status =
                    runningStatus;
            }
            else
            {
                if (status < 0xF0)
                {
                    runningStatus =
                        status;
                }
                else
                {
                    runningStatus = 0;
                }
            }

            const uint8_t command =
                status & 0xF0;

            const uint8_t channel =
                status & 0x0F;

            size_t dataCount = 0;

            switch (command)
            {
            case 0x80:
            case 0x90:
            case 0xA0:
            case 0xB0:
            case 0xE0:
                dataCount = 2;
                break;

            case 0xC0:
            case 0xD0:
                dataCount = 1;
                break;

            default:
                error = makeError(
                    trackIndex,
                    "unsupported MIDI channel event"
                );

                return false;
            }

            std::vector<uint8_t> data;

            data.reserve(dataCount);

            for (size_t i = 0; i < dataCount; ++i)
            {
                uint8_t value = 0;

                if (!reader.read8(value))
                {
                    error = makeError(
                        trackIndex,
                        "truncated MIDI channel event"
                    );

                    return false;
                }

                data.push_back(value);
            }

            MIDIEditorEvent event;

            event.tick =
                absoluteTick;

            event.kind =
                MIDIEditorEvent::Kind::MIDI;

            event.status =
                status;

            event.channel =
                channel;

            event.type =
                command;

            event.data =
                data;

            track.events.push_back(event);

            // Note-on with velocity zero is MIDI note-off.
            if (
                command == 0x90 &&
                data.size() >= 2 &&
                data[1] != 0
                )
            {
                PendingNote pending;

                pending.startTick =
                    absoluteTick;

                pending.velocity =
                    data[1];

                pendingNotes[
                    pendingIndex(
                        channel,
                        data[0]
                    )
                ].push_back(
                    pending
                );
            }
            else if (
                (
                    command == 0x80
                    ) ||
                (
                    command == 0x90 &&
                    data.size() >= 2 &&
                    data[1] == 0
                    )
                )
            {
                auto& pending =
                    pendingNotes[
                        pendingIndex(
                            channel,
                            data[0]
                        )
                    ];

                if (!pending.empty())
                {
                    const PendingNote note =
                        pending.front();

                    pending.erase(
                        pending.begin()
                    );

                    addExtractedNote(
                        document,
                        track,
                        channel,
                        data[0],
                        note,
                        absoluteTick
                    );
                }
            }
        }

        // A malformed/incomplete track may omit FF 2F 00.
        // We can still load it, but close notes at the final tick
        // so their durations remain usable.
        if (!reachedEndOfTrack)
        {
            for (
                uint16_t channel = 0;
                channel < 16;
                ++channel
                )
            {
                for (
                    uint16_t pitch = 0;
                    pitch < 128;
                    ++pitch
                    )
                {
                    auto& pending =
                        pendingNotes[
                            pendingIndex(
                                static_cast<uint8_t>(
                                    channel
                                    ),
                                static_cast<uint8_t>(
                                    pitch
                                    )
                            )
                        ];

                    for (
                        const auto& note :
                        pending
                        )
                    {
                        addExtractedNote(
                            document,
                            track,
                            static_cast<uint8_t>(
                                channel
                                ),
                            static_cast<uint8_t>(
                                pitch
                                ),
                            note,
                            absoluteTick
                        );
                    }
                }
            }
        }
        else
        {
            // A valid track can still have notes left active at EOT.
            for (
                uint16_t channel = 0;
                channel < 16;
                ++channel
                )
            {
                for (
                    uint16_t pitch = 0;
                    pitch < 128;
                    ++pitch
                    )
                {
                    auto& pending =
                        pendingNotes[
                            pendingIndex(
                                static_cast<uint8_t>(
                                    channel
                                    ),
                                static_cast<uint8_t>(
                                    pitch
                                    )
                            )
                        ];

                    for (
                        const auto& note :
                        pending
                        )
                    {
                        addExtractedNote(
                            document,
                            track,
                            static_cast<uint8_t>(
                                channel
                                ),
                            static_cast<uint8_t>(
                                pitch
                                ),
                            note,
                            absoluteTick
                        );
                    }
                }
            }
        }

        return true;
    }

    std::string extractFileName(
        const std::string& path
    )
    {
        const size_t slash =
            path.find_last_of(
                "/\\"
            );

        if (slash == std::string::npos)
            return path;

        return path.substr(
            slash + 1
        );
    }
}

bool MIDIEditorIO::load(
    const std::string& filePath,
    MIDIEditorDocument& document,
    std::string& error
)
{
    error.clear();

    std::ifstream input(
        filePath,
        std::ios::binary |
        std::ios::ate
    );

    if (!input.is_open())
    {
        error =
            "Couldn't open MIDI file: " +
            filePath;

        return false;
    }

    const std::streamsize size =
        input.tellg();

    if (size < 14)
    {
        error =
            "MIDI file is too small to contain a valid header.";

        return false;
    }

    input.seekg(
        0,
        std::ios::beg
    );

    std::vector<uint8_t> bytes(
        static_cast<size_t>(
            size
            )
    );

    if (!input.read(
        reinterpret_cast<char*>(
            bytes.data()
            ),
        size
    ))
    {
        error =
            "Couldn't read MIDI file: " +
            filePath;

        return false;
    }

    ByteReader reader(bytes);

    uint32_t headerMagic = 0;
    uint32_t headerLength = 0;

    uint16_t format = 0;
    uint16_t trackCount = 0;
    uint16_t division = 0;

    if (
        !reader.read32BE(
            headerMagic
        ) ||
        !reader.read32BE(
            headerLength
        ) ||
        headerMagic != 0x4D546864 ||
        headerLength != 6
        )
    {
        error =
            "Invalid MIDI header.";

        return false;
    }

    if (
        !reader.read16BE(
            format
        ) ||
        !reader.read16BE(
            trackCount
        ) ||
        !reader.read16BE(
            division
        )
        )
    {
        error =
            "Truncated MIDI header.";

        return false;
    }

    if (format > 1)
    {
        error =
            "Unsupported MIDI format. "
            "Only type 0 and type 1 are supported.";

        return false;
    }

    if (trackCount == 0)
    {
        error =
            "MIDI file contains no tracks.";

        return false;
    }

    // The editor currently operates in PPQ/tick space.
    if ((division & 0x8000u) != 0)
    {
        error =
            "SMPTE-based MIDI timing is not supported by the editor yet. "
            "The file must use PPQ timing.";

        return false;
    }

    const uint16_t ticksPerQuarterNote =
        static_cast<uint16_t>(
            division & 0x7FFFu
            );

    if (ticksPerQuarterNote == 0)
    {
        error =
            "MIDI file has an invalid ticks-per-quarter-note value.";

        return false;
    }

    MIDIEditorDocument loaded;

    loaded.clear();

    loaded.format =
        format == 0
        ? MIDIEditorFormat::SingleTrack
        : MIDIEditorFormat::MultiTrack;

    loaded.ticksPerQuarterNote =
        ticksPerQuarterNote;

    loaded.filePath =
        filePath;

    loaded.fileName =
        extractFileName(
            filePath
        );

    loaded.tracks.reserve(
        trackCount
    );

    for (
        uint16_t trackIndex = 0;
        trackIndex < trackCount;
        ++trackIndex
        )
    {
        uint32_t trackMagic = 0;
        uint32_t trackLength = 0;

        if (
            !reader.read32BE(
                trackMagic
            ) ||
            !reader.read32BE(
                trackLength
            )
            )
        {
            std::ostringstream stream;

            stream
                << "Truncated MIDI track header at track "
                << trackIndex
                << ".";

            error =
                stream.str();

            return false;
        }

        if (trackMagic != 0x4D54726B)
        {
            std::ostringstream stream;

            stream
                << "Invalid MTrk header at track "
                << trackIndex
                << ".";

            error =
                stream.str();

            return false;
        }

        if (!reader.canRead(
            trackLength
        ))
        {
            std::ostringstream stream;

            stream
                << "Track "
                << trackIndex
                << " extends beyond end of file.";

            error =
                stream.str();

            return false;
        }

        std::vector<uint8_t> trackBytes;

        if (!reader.readBytes(
            trackLength,
            trackBytes
        ))
        {
            std::ostringstream stream;

            stream
                << "Couldn't read track "
                << trackIndex
                << ".";

            error =
                stream.str();

            return false;
        }

        loaded.tracks.emplace_back();

        if (!parseTrack(
            trackBytes,
            trackIndex,
            loaded,
            loaded.tracks.back(),
            error
        ))
        {
            return false;
        }
    }

    // MIDI files are allowed to omit these meta events.
    // Give the editor sensible defaults while retaining any real
    // events that were present.
    if (loaded.tempos.empty())
    {
        loaded.tempos.push_back({
            0,
            500000
            });
    }
    else if (loaded.tempos.front().tick != 0)
    {
        // A tempo map always has a well-defined tempo from tick 0 onward.
        // Preserve the real tempo event and prepend the standard MIDI default.
        loaded.tempos.insert(
            loaded.tempos.begin(),
            {
                0,
                500000
            }
        );
    }

    if (loaded.timeSignatures.empty())
    {
        loaded.timeSignatures.push_back({
            0,
            4,
            2
            });
    }
    else if (
        loaded.timeSignatures.front().tick != 0
        )
    {
        loaded.timeSignatures.insert(
            loaded.timeSignatures.begin(),
            {
                0,
                4,
                2
            }
        );
    }

    loaded.sortNotes();

    loaded.dirty = false;

    document =
        std::move(
            loaded
        );

    return true;
}

namespace
{
    struct MIDIWriteEvent
    {
        uint64_t tick = 0;
        int priority = 0;
        size_t order = 0;

        std::vector<uint8_t> bytes;
    };

    void appendBE16(
        std::vector<uint8_t>& output,
        uint16_t value
    )
    {
        output.push_back(
            static_cast<uint8_t>(
                (value >> 8) & 0xFFu
                )
        );

        output.push_back(
            static_cast<uint8_t>(
                value & 0xFFu
                )
        );
    }

    void appendBE32(
        std::vector<uint8_t>& output,
        uint32_t value
    )
    {
        output.push_back(
            static_cast<uint8_t>(
                (value >> 24) & 0xFFu
                )
        );

        output.push_back(
            static_cast<uint8_t>(
                (value >> 16) & 0xFFu
                )
        );

        output.push_back(
            static_cast<uint8_t>(
                (value >> 8) & 0xFFu
                )
        );

        output.push_back(
            static_cast<uint8_t>(
                value & 0xFFu
                )
        );
    }

    bool appendVariableLengthQuantity(
        std::vector<uint8_t>& output,
        uint64_t value
    )
    {
        // Standard MIDI delta-times are limited to 28 bits.
        if (value > 0x0FFFFFFFu)
            return false;

        uint8_t buffer[4]{};

        int count = 0;

        buffer[count++] =
            static_cast<uint8_t>(
                value & 0x7Fu
                );

        while (
            (value >>= 7u) != 0
            )
        {
            buffer[count++] =
                static_cast<uint8_t>(
                    (value & 0x7Fu) |
                    0x80u
                    );
        }

        for (
            int i = count - 1;
            i >= 0;
            --i
            )
        {
            output.push_back(
                buffer[i]
            );
        }

        return true;
    }

    bool isNoteEvent(
        const MIDIEditorEvent& event
    )
    {
        if (
            event.kind !=
            MIDIEditorEvent::Kind::MIDI
            )
        {
            return false;
        }

        const uint8_t command =
            event.status & 0xF0u;

        return
            command == 0x80u ||
            command == 0x90u;
    }

    bool encodeRawEvent(
        const MIDIEditorEvent& event,
        std::vector<uint8_t>& bytes,
        std::string& error
    )
    {
        bytes.clear();

        if (
            event.kind ==
            MIDIEditorEvent::Kind::MIDI
            )
        {
            if (
                (event.status & 0x80u) == 0 ||
                event.status >= 0xF0u
                )
            {
                error =
                    "Invalid MIDI channel-event status byte.";

                return false;
            }

            bytes.push_back(
                event.status
            );

            bytes.insert(
                bytes.end(),
                event.data.begin(),
                event.data.end()
            );

            return true;
        }

        if (
            event.kind ==
            MIDIEditorEvent::Kind::META
            )
        {
            if (
                event.data.size() >
                0x0FFFFFFFu
                )
            {
                error =
                    "MIDI meta event is too large.";

                return false;
            }

            bytes.push_back(
                0xFFu
            );

            bytes.push_back(
                event.type
            );

            if (!appendVariableLengthQuantity(
                bytes,
                static_cast<uint64_t>(
                    event.data.size()
                    )
            ))
            {
                error =
                    "Failed to encode MIDI meta-event length.";

                return false;
            }

            bytes.insert(
                bytes.end(),
                event.data.begin(),
                event.data.end()
            );

            return true;
        }

        if (
            event.kind ==
            MIDIEditorEvent::Kind::SYSEX
            )
        {
            if (
                event.data.size() >
                0x0FFFFFFFu
                )
            {
                error =
                    "MIDI SysEx event is too large.";

                return false;
            }

            bytes.push_back(
                event.status
            );

            if (!appendVariableLengthQuantity(
                bytes,
                static_cast<uint64_t>(
                    event.data.size()
                    )
            ))
            {
                error =
                    "Failed to encode MIDI SysEx length.";

                return false;
            }

            bytes.insert(
                bytes.end(),
                event.data.begin(),
                event.data.end()
            );

            return true;
        }

        error =
            "Unknown MIDI event kind.";

        return false;
    }
}

bool MIDIEditorIO::save(
    const std::string& filePath,
    const MIDIEditorDocument& document,
    std::string& error
)
{
    error.clear();

    if (filePath.empty())
    {
        error =
            "Couldn't save MIDI file: file path is empty.";

        return false;
    }

    if (document.tracks.empty())
    {
        error =
            "Couldn't save MIDI file: document contains no tracks.";

        return false;
    }

    if (
        document.ticksPerQuarterNote == 0 ||
        document.ticksPerQuarterNote > 0x7FFFu
        )
    {
        error =
            "Couldn't save MIDI file: invalid "
            "ticks-per-quarter-note value.";

        return false;
    }

    if (
        document.format ==
        MIDIEditorFormat::SingleTrack &&
        document.tracks.size() != 1
        )
    {
        error =
            "Couldn't save MIDI format 0: format 0 documents "
            "must contain exactly one track.";

        return false;
    }

    if (
        document.tracks.size() >
        static_cast<size_t>(
            std::numeric_limits<uint16_t>::max()
            )
        )
    {
        error =
            "Couldn't save MIDI file: too many tracks.";

        return false;
    }

    std::vector<
        std::vector<uint8_t>
    > trackData;

    trackData.reserve(
        document.tracks.size()
    );

    for (
        size_t trackIndex = 0;
        trackIndex < document.tracks.size();
        ++trackIndex
        )
    {
        const MIDIEditorTrack& track =
            document.tracks[
                trackIndex
            ];

        std::vector<MIDIWriteEvent> events;

        events.reserve(
            track.events.size() +
            track.notes.size() * 2 +
            1
        );

        size_t order = 0;

        uint64_t finalTick = 0;

        // Preserve every raw event except note-on/note-off events.
        // Note events are regenerated from the editable note model below.
        for (
            const auto& event :
            track.events
            )
        {
            // End-of-track is regenerated after all editable notes.
            if (
                event.kind ==
                MIDIEditorEvent::Kind::META &&
                event.type == 0x2F
                )
            {
                finalTick =
                    (std::max)(
                        finalTick,
                        event.tick
                        );

                ++order;

                continue;
            }

            if (isNoteEvent(event))
            {
                finalTick =
                    (std::max)(
                        finalTick,
                        event.tick
                        );

                ++order;

                continue;
            }

            MIDIWriteEvent outputEvent;

            outputEvent.tick =
                event.tick;

            outputEvent.priority =
                1;

            outputEvent.order =
                order++;

            if (!encodeRawEvent(
                event,
                outputEvent.bytes,
                error
            ))
            {
                std::ostringstream stream;

                stream
                    << "Track "
                    << trackIndex
                    << ": "
                    << error;

                error =
                    stream.str();

                return false;
            }

            events.push_back(
                std::move(
                    outputEvent
                )
            );

            finalTick =
                (std::max)(
                    finalTick,
                    event.tick
                    );
        }

        // Recreate editable notes as MIDI note-on/note-off events.
        for (
            const auto& note :
            track.notes
            )
        {
            if (
                note.pitch < 0 ||
                note.pitch > 127
                )
            {
                std::ostringstream stream;

                stream
                    << "Track "
                    << trackIndex
                    << ": note has invalid pitch "
                    << note.pitch
                    << ".";

                error =
                    stream.str();

                return false;
            }

            if (
                note.velocity < 0 ||
                note.velocity > 127
                )
            {
                std::ostringstream stream;

                stream
                    << "Track "
                    << trackIndex
                    << ": note has invalid velocity "
                    << note.velocity
                    << ".";

                error =
                    stream.str();

                return false;
            }

            if (
                note.channel < 0 ||
                note.channel > 15
                )
            {
                std::ostringstream stream;

                stream
                    << "Track "
                    << trackIndex
                    << ": note has invalid MIDI channel "
                    << note.channel
                    << ".";

                error =
                    stream.str();

                return false;
            }

            if (
                note.durationTick >
                std::numeric_limits<uint64_t>::max() -
                note.startTick
                )
            {
                std::ostringstream stream;

                stream
                    << "Track "
                    << trackIndex
                    << ": note duration overflows "
                    << "the MIDI tick range.";

                error =
                    stream.str();

                return false;
            }

            const uint64_t endTick =
                note.startTick +
                note.durationTick;

            MIDIWriteEvent noteOff;

            noteOff.tick =
                endTick;

            noteOff.priority =
                0;

            noteOff.order =
                order++;

            noteOff.bytes =
            {
                static_cast<uint8_t>(
                    0x80u |
                    static_cast<uint8_t>(
                        note.channel
                    )
                ),

                static_cast<uint8_t>(
                    note.pitch
                ),

                0
            };

            MIDIWriteEvent noteOn;

            noteOn.tick =
                note.startTick;

            noteOn.priority =
                2;

            noteOn.order =
                order++;

            noteOn.bytes =
            {
                static_cast<uint8_t>(
                    0x90u |
                    static_cast<uint8_t>(
                        note.channel
                    )
                ),

                static_cast<uint8_t>(
                    note.pitch
                ),

                static_cast<uint8_t>(
                    note.velocity
                )
            };

            events.push_back(
                std::move(
                    noteOff
                )
            );

            events.push_back(
                std::move(
                    noteOn
                )
            );

            finalTick =
                (std::max)(
                    finalTick,
                    endTick
                    );
        }

        MIDIWriteEvent endOfTrack;

        endOfTrack.tick =
            finalTick;

        endOfTrack.priority =
            3;

        endOfTrack.order =
            order++;

        endOfTrack.bytes =
        {
            0xFFu,
            0x2Fu,
            0x00u
        };

        events.push_back(
            std::move(
                endOfTrack
            )
        );

        std::stable_sort(
            events.begin(),
            events.end(),
            [](const MIDIWriteEvent& a,
                const MIDIWriteEvent& b)
            {
                if (a.tick != b.tick)
                    return a.tick < b.tick;

                if (a.priority != b.priority)
                    return a.priority < b.priority;

                return a.order < b.order;
            }
        );

        std::vector<uint8_t> encodedTrack;

        encodedTrack.reserve(
            events.size() * 8
        );

        uint64_t previousTick = 0;

        for (
            const auto& event :
            events
            )
        {
            if (
                event.tick <
                previousTick
                )
            {
                error =
                    "Couldn't save MIDI file: events "
                    "are not sorted by tick.";

                return false;
            }

            const uint64_t delta =
                event.tick -
                previousTick;

            if (!appendVariableLengthQuantity(
                encodedTrack,
                delta
            ))
            {
                std::ostringstream stream;

                stream
                    << "Track "
                    << trackIndex
                    << ": delta-time exceeds "
                    << "the standard MIDI 28-bit limit.";

                error =
                    stream.str();

                return false;
            }

            encodedTrack.insert(
                encodedTrack.end(),
                event.bytes.begin(),
                event.bytes.end()
            );

            previousTick =
                event.tick;
        }

        if (
            encodedTrack.size() >
            static_cast<size_t>(
                std::numeric_limits<uint32_t>::max()
                )
            )
        {
            std::ostringstream stream;

            stream
                << "Track "
                << trackIndex
                << ": encoded track is too large.";

            error =
                stream.str();

            return false;
        }

        trackData.push_back(
            std::move(
                encodedTrack
            )
        );
    }

    std::vector<uint8_t> output;

    output.reserve(
        14
    );

    // MThd
    output.push_back('M');
    output.push_back('T');
    output.push_back('h');
    output.push_back('d');

    appendBE32(
        output,
        6
    );

    const uint16_t format =
        document.format ==
        MIDIEditorFormat::SingleTrack
        ? 0
        : 1;

    appendBE16(
        output,
        format
    );

    appendBE16(
        output,
        static_cast<uint16_t>(
            trackData.size()
            )
    );

    appendBE16(
        output,
        document.ticksPerQuarterNote
    );

    for (
        const auto& track :
        trackData
        )
    {
        output.push_back('M');
        output.push_back('T');
        output.push_back('r');
        output.push_back('k');

        appendBE32(
            output,
            static_cast<uint32_t>(
                track.size()
                )
        );

        output.insert(
            output.end(),
            track.begin(),
            track.end()
        );
    }

    // Write a temporary sibling first. This avoids leaving the existing MIDI
    // file truncated if writing the new file fails partway through.
    const std::filesystem::path targetPath(
        filePath
    );

    const std::filesystem::path temporaryPath =
        targetPath.string() +
        ".pianoeditor.tmp";

    {
        std::ofstream outputFile(
            temporaryPath,
            std::ios::binary |
            std::ios::trunc
        );

        if (!outputFile.is_open())
        {
            error =
                "Couldn't open temporary MIDI file "
                "for writing: " +
                temporaryPath.string();

            return false;
        }

        if (!output.empty())
        {
            outputFile.write(
                reinterpret_cast<const char*>(
                    output.data()
                    ),
                static_cast<std::streamsize>(
                    output.size()
                    )
            );
        }

        if (!outputFile.good())
        {
            outputFile.close();

            std::error_code removeError;

            std::filesystem::remove(
                temporaryPath,
                removeError
            );

            error =
                "Couldn't write MIDI file: " +
                temporaryPath.string();

            return false;
        }
    }

    std::error_code copyError;

    std::filesystem::copy_file(
        temporaryPath,
        targetPath,
        std::filesystem::copy_options::overwrite_existing,
        copyError
    );

    std::error_code removeError;

    std::filesystem::remove(
        temporaryPath,
        removeError
    );

    if (copyError)
    {
        error =
            "Couldn't overwrite MIDI file '" +
            filePath +
            "': " +
            copyError.message();

        return false;
    }

    return true;
}