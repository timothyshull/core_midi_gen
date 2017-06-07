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
class Core_midi_gen {
private:
    // added to remove magic number in _play_loop
    static const std::chrono::seconds _loop_sleep_dur;
    // CoreAudio and CoreMIDI are C-based APIs so they have manual functions for clean-up
    // for safety, can wrap pointer-based structures in unique_ptr with custom deleter
    Arg_parser _arg_parser;
    Au_graph_manager _graph_manager;
    MusicSequence _sequence = nullptr;
    AudioUnit _synth = nullptr;
    MusicPlayer _player = nullptr;
    std::set<int> &_track_set;


public:
    // next steps -> break up control flow here into functions
    // TODO: may want to rethink control flow because it basically does not do anything if -p is not passed
    //
    // (aside from load and print sequence from file)
    Core_midi_gen(int argc, char *argv[]);

    ~Core_midi_gen();

private:
    // removed return of OSStatus because all framework function calls are checked
    void _load_midi_file_to_sequence();

    void _write_output_file(MusicTimeStamp sequence_length);

    void _play_loop(MusicTimeStamp sequence_length);

    void _play_sequence();

    void _setup_midi_endpoint();

    void _setup_alternate_output();
};

#endif //CORE_MIDI_GEN2_CORE_MIDI_GEN_H
