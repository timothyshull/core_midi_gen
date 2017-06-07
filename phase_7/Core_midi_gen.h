#ifndef CORE_MIDI_GEN2_CORE_MIDI_GEN_H
#define CORE_MIDI_GEN2_CORE_MIDI_GEN_H

#include <CoreFoundation/CoreFoundation.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudioTypes.h>

#include <CAAudioFileFormats.h>
#include <CAHostTimeBase.h>

#include <set>
#include <string>
#include <vector>

#include "Arg_parser.h"

#include "globals.h"
#include "util.h"
#include "Au_graph_manager.h"

// At this point, the anti-pattern is that this class has become a monolithic class
// TODO: should convert all possible in-out args to returning tuples
// TODO: determine how to reduce dependencies between the 3 main classes
class Core_midi_gen {
private:
    // added to remove magic number in _play_loop
    static const std::chrono::seconds _loop_sleep_dur;
    // CoreAudio and CoreMIDI are C-based APIs so they have manual functions for clean-up
    // for safety, can wrap pointer-based structures in unique_ptr with custom deleter
    Arg_parser &_arg_parser;
    Au_graph_manager _graph_manager;
    MusicSequence _sequence = nullptr;
    AudioUnit _synth = nullptr;
    MusicPlayer _player = nullptr;
    std::set<int> &_track_set;

public:
    Core_midi_gen(Arg_parser &arg_parser);

    ~Core_midi_gen();

    void run();

private:
    void _load_midi_file_to_sequence();

    CAStreamBasicDescription _gen_basic_description(AudioFileTypeID &dest_file_type);

    ExtAudioFileRef _prepare_outfile_for_writing();

    AudioUnit _prepare_output_au_for_writing();

    void _write_buffer_to_outfile(
            MusicTimeStamp sequence_length,
            const CAStreamBasicDescription &client_format,
            const ExtAudioFileRef outfile,
            AudioUnit output_unit
    );

    void _write_output_file(MusicTimeStamp sequence_length);

    static void _print_overloads();

    void _print_load(const MusicTimeStamp &time);

    void _print_info_while_playing(MusicTimeStamp sequence_length);

    void _init_sequence();

    void _init_single_track(UInt32 track_num, MusicTimeStamp &sequence_length, bool should_print_tracks);

    void _init_tracks(MusicTimeStamp &sequence_length);

    void _play_sequence();

    void _setup_midi_endpoint();

    void _setup_alternate_output();
};

#endif //CORE_MIDI_GEN2_CORE_MIDI_GEN_H
