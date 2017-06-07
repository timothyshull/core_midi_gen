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

#include "globals.h"
#include "util.h"

// Arg_parser and Core_midi_gen are highly dependent classes
// Despite this, I prefer flat classes over nested classes
class Arg_parser {
public:
    std::vector<std::string> args;
    std::string file_path = std::string{};
    int argc;
    bool should_play = false;
    bool should_set_bank = false;
    bool should_use_midi_endpoint = false;
    bool should_print = true;
    bool wait_at_end = false;
    bool disk_stream = false;
    OSType data_format = OSType{0};
    Float64 srate = Float64{0};
    std::string output_file_path = std::string{};
    MusicSequenceLoadFlags load_flags = kMusicSequenceLoadSMF_PreserveTracks;
    std::string bank_path = std::string{};
    Float32 start_time = Float32{0};
    UInt32 num_frames = UInt32{512};
    std::set<int> track_set = std::set<int>{};

    Arg_parser(int argc, char *argv[]);

    ~Arg_parser() = default;

private:
    void _set_args(int argc, char *argv[]);

    void _parse_args();

    void _malformed_input();

    void _check_file_path();

    void _check_midi_endpoint();
};

// At this point, the anti-pattern is that this class has become a monolithic class
class Core_midi_gen {
private:
    // CoreAudio and CoreMIDI are C-based APIs so they have manual functions for clean-up
    // for safety, can wrap pointer-based structures in unique_ptr with custom deleter
    Arg_parser _arg_parser;
    MusicSequence _sequence = nullptr;
    AUGraph _graph = nullptr; // non-owning -> need to handle
    AudioUnit _synth = nullptr;
    MusicPlayer _player = nullptr;
    std::set<int>& _track_set;
    // added to remove magic number in _play_loop
    static const std::chrono::seconds _loop_sleep_dur;

public:
    // next steps -> break up control flow here into functions
    // TODO: may want to rethink control flow because it basically does not do anything if -p is not passed (aside from load and print sequence from file)
    // TODO: either make all private functions return OSStatus or none
    Core_midi_gen(int argc, char *argv[]);

    ~Core_midi_gen();

private:
    OSStatus _load_smf();

    OSStatus _get_synth_from_au_graph();

    OSStatus _set_up_graph();

    void _write_output_file(MusicTimeStamp sequence_length);

    void _play_loop(MusicTimeStamp sequence_length);

    void _play_sequence();

    void _setup_midi_endpoint();

    void _setup_alternate_output();
};

#endif //CORE_MIDI_GEN2_CORE_MIDI_GEN_H
