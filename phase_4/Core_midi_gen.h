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
    std::set<int> _track_set = std::set<int>{};

public:
    // next steps -> break up control flow here into functions
    // TODO: may want to rethink control flow because it basically does not do anything if -p is not passed (aside from load and print sequence from file)
    Core_midi_gen(int argc, char *argv[])
            : _arg_parser{argc, argv}
    {
        auto result = load_smf(_arg_parser.file_path, _sequence, _arg_parser.load_flags);
        check_error(result, "load_smf");

        if (_arg_parser.should_print) {
            CAShow(_sequence);
        }

        // moved clean-up to dtor
        if (_arg_parser.should_play) {
            _play_sequence();
        }

        while (_arg_parser.wait_at_end) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, false);
        }
    }

    ~Core_midi_gen() {
        // resource disposal
        if (_arg_parser.should_play) {
            auto result = DisposeMusicPlayer(_player);
            check_error(result, "DisposeMusicPlayer");
        }
        auto result = DisposeMusicSequence(_sequence);
        check_error(result, "DisposeMusicSequence");
        // don't own the graph so don't dispose it (the seq owns it as we never set it ourselves, we just got it....)
    }

    // TODO: remove unnecessary arguments
    OSStatus load_smf(const std::string &filename, MusicSequence &sequence, MusicSequenceLoadFlags load_flags);

    OSStatus get_synth_from_au_graph(AUGraph &in_graph, AudioUnit &out_synth);

    OSStatus set_up_graph(AUGraph &in_graph, UInt32 num_frames, Float64 &out_sample_rate, bool is_offline);

    void write_output_file(
            const char *output_file_path,
            OSType data_format,
            Float64 srate,
            MusicTimeStamp sequence_length,
            bool should_print,
            AUGraph in_graph,
            UInt32 num_frames,
            MusicPlayer player
    );

    void play_loop(
            MusicPlayer &player,
            AUGraph &graph,
            MusicTimeStamp sequence_length,
            bool should_print,
            bool wait_at_end
    );

private:
    void _play_sequence() {
        auto result = MusicSequenceGetAUGraph(_sequence, &_graph);
        check_error(result, "MusicSequenceGetAUGraph");

        result = AUGraphOpen(_graph);
        check_error(result, "AUGraphOpen");

        result = get_synth_from_au_graph(_graph, _synth);
        check_error(result, "GetSynthFromGraph");

        result = AudioUnitSetProperty(
                _synth,
                kAudioUnitProperty_CPULoad,
                kAudioUnitScope_Global,
                0,
                &globals::max_cpu_load,
                sizeof(globals::max_cpu_load)
        );
        check_error(result, "AudioUnitSetProperty: kAudioUnitProperty_CPULoad");

        if (_arg_parser.should_use_midi_endpoint) {
            _setup_midi_endpoint();
        } else {
            _setup_alternate_output();
        }

        result = NewMusicPlayer(&_player);
        check_error(result, "NewMusicPlayer");

        result = MusicPlayerSetSequence(_player, _sequence);
        check_error(result, "MusicPlayerSetSequence");

        // figure out sequence length
        auto num_tracks = UInt32{};

        result = MusicSequenceGetTrackCount(_sequence, &num_tracks);
        check_error(result, "MusicSequenceGetTrackCount");

        auto sequence_length = MusicTimeStamp{0.};

        auto should_print_tracks = _arg_parser.should_print && !_track_set.empty();
        if (should_print_tracks) {
            printf("Only playing specified tracks:\n\t");
        }

        for (auto i = static_cast<UInt32>(0); i < num_tracks; ++i) {
            auto track = static_cast<MusicTrack>(nullptr);
            auto track_length = MusicTimeStamp{};
            auto prop_size = static_cast<UInt32>(sizeof(MusicTimeStamp));

            result = MusicSequenceGetIndTrack(_sequence, i, &track);
            check_error(result, "MusicSequenceGetIndTrack");

            result = MusicTrackGetProperty(
                    track,
                    kSequenceTrackProperty_TrackLength,
                    &track_length,
                    &prop_size
            );
            check_error(result, "MusicTrackGetProperty: kSequenceTrackProperty_TrackLength");

            if (track_length > sequence_length) {
                sequence_length = track_length;
            }

            if (!_track_set.empty() && (_track_set.find(i) == _track_set.end())) {
                auto mute = Boolean{true};
                result = MusicTrackSetProperty(track, kSequenceTrackProperty_MuteStatus, &mute, sizeof(mute));
                check_error(result, "MusicTrackSetProperty: kSequenceTrackProperty_MuteStatus");
            } else if (should_print_tracks) {
                printf("%d, ", int(i + 1));
            }
        }

        if (should_print_tracks) {
            printf("\n");
        }

        // now I'm going to add 8 beats on the end for the reverb/long releases to tail off...
        sequence_length += 8;

        result = MusicPlayerSetTime(_player, _arg_parser.start_time);
        check_error(result, "MusicPlayerSetTime");

        result = MusicPlayerPreroll(_player);
        check_error(result, "MusicPlayerPreroll");

        if (_arg_parser.should_print) {
            printf("Ready to play: %s, %.2f beats long\n\t<Enter> to continue: ", _arg_parser.file_path.c_str(),
                   sequence_length
            );
            getc(stdin);
        }

        globals::start_running_time = CAHostTimeBase::GetTheCurrentTime();

        result = MusicPlayerStart(_player);
        check_error(result, "MusicPlayerStart");

        if (_arg_parser.output_file_path != "") {
            write_output_file(
                    _arg_parser.output_file_path.c_str(),
                    _arg_parser.data_format,
                    _arg_parser.srate,
                    sequence_length,
                    _arg_parser.should_print,
                    _graph,
                    _arg_parser.num_frames,
                    _player
            );
        } else {
            play_loop(_player, _graph, sequence_length, _arg_parser.should_print, _arg_parser.wait_at_end);
        }

        result = MusicPlayerStop(_player);
        check_error(result, "MusicPlayerStop");
        if (_arg_parser.should_print) { printf("finished playing\n"); }

        // moved clean-up to dtor
    }

    void _setup_midi_endpoint() {
        auto midi_client = MIDIClientRef{};
        auto result = MIDIClientCreate(CFSTR("Play Sequence"), nullptr, nullptr, &midi_client);
        check_error(result, "MIDIClientCreate");

        auto dest_count = MIDIGetNumberOfDestinations();
        if (dest_count == 0) {
            fprintf(stderr, "No MIDI Endpoints to play to.\n");
            exit(1);
        }

        result = MusicSequenceSetMIDIEndpoint(_sequence, MIDIGetDestination(0));
        check_error(result, "MusicSequenceSetMIDIEndpoint");
    }

    void _setup_alternate_output() {
        if (_arg_parser.should_set_bank) {
            auto sound_bank_url = CFURLCreateFromFileSystemRepresentation(
                    kCFAllocatorDefault,
                    reinterpret_cast<const UInt8 *>(_arg_parser.bank_path.c_str()), // unsafe
                    _arg_parser.bank_path.size(),
                    false
            );

            printf("Setting Sound Bank:%s\n", _arg_parser.bank_path.c_str());

            auto result = AudioUnitSetProperty(
                    _synth,
                    kMusicDeviceProperty_SoundBankURL,
                    kAudioUnitScope_Global,
                    0,
                    &sound_bank_url,
                    sizeof(sound_bank_url)
            );
            if (sound_bank_url) {
                CFRelease(sound_bank_url);
            }
            check_error(result, "AudioUnitSetProperty: kMusicDeviceProperty_SoundBankURL");
        }

        if (_arg_parser.disk_stream) {
            auto value = static_cast<UInt32>(_arg_parser.disk_stream);
            auto result = AudioUnitSetProperty(
                    _synth,
                    kMusicDeviceProperty_StreamFromDisk,
                    kAudioUnitScope_Global,
                    0,
                    &value,
                    sizeof(value)
            );
            check_error(result, "AudioUnitSetProperty: kMusicDeviceProperty_StreamFromDisk");
        }

        if (_arg_parser.output_file_path != "") {
            // need to tell synth that is going to render a file.
            auto value = UInt32{1};
            auto result = AudioUnitSetProperty(
                    _synth,
                    kAudioUnitProperty_OfflineRender,
                    kAudioUnitScope_Global,
                    0,
                    &value,
                    sizeof(value)
            );
            check_error(result, "AudioUnitSetProperty: kAudioUnitProperty_OfflineRender");
        }

        auto result = set_up_graph(_graph, _arg_parser.num_frames, _arg_parser.srate,
                              (_arg_parser.output_file_path != ""));
        check_error(result, "SetUpGraph");

        if (_arg_parser.should_print) {
            printf("Sample Rate: %.1f \n", _arg_parser.srate);
            printf("Disk Streaming is enabled: %c\n", (_arg_parser.disk_stream ? 'T' : 'F'));
        }

        result = AUGraphInitialize(_graph);
        check_error(result, "AUGraphInitialize");

        if (_arg_parser.should_print) {
            CAShow(_graph);
        }
    }
};

#endif //CORE_MIDI_GEN2_CORE_MIDI_GEN_H
