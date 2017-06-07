#include <CoreFoundation/CoreFoundation.h>
#include <CoreAudio/CoreAudioTypes.h>

#include <set>
#include <vector>

#include "CAAudioFileFormats.h"
#include "CAHostTimeBase.h"

#include "Core_midi_gen.h"
#include "util.h"

int main(int argc, const char *argv[])
{
    if (argc == 1) {
        fprintf(stderr, "%s\n", globals::usage_string.c_str());
        exit(0);
    }

    auto file_path = std::string{};
    auto should_play = false;
    auto should_set_bank = false;
    auto should_use_midi_endpoint = false;
    auto should_print = true;
    auto wait_at_end = false;
    auto disk_stream = false;

    auto data_format = OSType{0};
    auto srate = Float64{0};
    auto output_file_path = std::string{};

    auto load_flags = kMusicSequenceLoadSMF_PreserveTracks;

    auto bank_path = static_cast<char *>(nullptr);

    auto start_time = Float32{0};
    auto num_frames = UInt32{512};

    auto track_set = std::set<int>{};

    auto args = [argc, argv]() {
        auto res = std::vector<std::string>(static_cast<std::vector<std::string>::size_type>(argc));
        for (auto i = 0; i < argc; ++i) {
            res[i] = argv[i];
        }
        return res;
    }();

    auto malformed_input = []() {
        fprintf(stderr, "%s\n", globals::usage_string.c_str());
        exit(1);
    };

    for (auto i = 1; i < argc; ++i) {
        if (args[i] == "-p") {
            should_play = true;
        } else if (args[i] == "-w") {
            wait_at_end = true;
        } else if (args[i] == "-d") {
            disk_stream = true;
        } else if (args[i] == "-b") {
            should_set_bank = true;
            if (++i == argc) {
                malformed_input();
            }
            bank_path = const_cast<char *>(argv[i]);
        } else if (args[i] == "-n") {
            should_print = false;
        } else if ((file_path == "") && (args[i][0] == '/' || args[i][0] == '~')) {
            file_path = args[i];
        } else if (args[i] == "-s") {
            if (++i == argc) {
                malformed_input();
            }
            start_time = lexical_cast<decltype(start_time), decltype(args[i])>(args[i]);
        } else if (args[i] == "-t") {
            if (++i == argc) {
                malformed_input();
            }
            auto index = lexical_cast<int, decltype(args[i])>(args[i]);
            track_set.insert(--index);
        } else if (args[i] == "-e") {
            should_use_midi_endpoint = true;
        } else if (args[i] == "-c") {
            load_flags = kMusicSequenceLoadSMF_ChannelsToTracks;
        } else if (args[i] == "-i") {
            if (++i == argc) {
                malformed_input();
            }
            sscanf(argv[i], "%lu", (unsigned long *) (&num_frames));
        } else if (args[i] == "-f") {
            if (i + 3 >= argc) {
                malformed_input();
            }
            output_file_path = args[++i];
            StrToOSType(args[++i].c_str(), data_format);
            ++i;
            srate = lexical_cast<decltype(srate), decltype(args[i])>(args[i]);
        } else {
            malformed_input();
        }
    }

    if (file_path == "") {
        fprintf(stderr, "You have to specify a MIDI file to print or play\n");
        fprintf(stderr, "%s\n", globals::usage_string.c_str());
        exit(1);
    }

    if (should_use_midi_endpoint && output_file_path != "") {
        printf("can't write a file when you try to play out to a MIDI Endpoint\n");
        exit(1);
    }

    auto sequence = static_cast<MusicSequence>(nullptr);
    auto result = load_smf(file_path, sequence, load_flags);
    check_error(result, "load_smf");

    if (should_print) {
        CAShow(sequence);
    }

    if (should_play) {
        auto graph = static_cast<AUGraph>(nullptr);
        auto synth = static_cast<AudioUnit>(nullptr);

        result = MusicSequenceGetAUGraph(sequence, &graph);
        check_error(result, "MusicSequenceGetAUGraph");

        result = AUGraphOpen(graph);
        check_error(result, "AUGraphOpen");

        result = get_synth_from_au_graph(graph, synth);
        check_error(result, "GetSynthFromGraph");

        result = AudioUnitSetProperty(
                synth,
                kAudioUnitProperty_CPULoad,
                kAudioUnitScope_Global,
                0,
                &globals::max_cpu_load,
                sizeof(globals::max_cpu_load)
        );
        check_error(result, "AudioUnitSetProperty: kAudioUnitProperty_CPULoad");

        if (should_use_midi_endpoint) {
            auto the_midi_client = MIDIClientRef{};
            result = MIDIClientCreate(CFSTR("Play Sequence"), nullptr, nullptr, &the_midi_client);
            check_error(result, "MIDIClientCreate");

            auto dest_count = MIDIGetNumberOfDestinations();
            if (dest_count == 0) {
                fprintf(stderr, "No MIDI Endpoints to play to.\n");
                exit(1);
            }

            result = MusicSequenceSetMIDIEndpoint(sequence, MIDIGetDestination(0));
            check_error(result, "MusicSequenceSetMIDIEndpoint");
        } else {
            if (should_set_bank) {
                auto sound_bank_url = CFURLCreateFromFileSystemRepresentation(
                        kCFAllocatorDefault,
                        reinterpret_cast<const UInt8 *>(bank_path), // unsafe
                        strlen(bank_path),
                        false
                );

                printf("Setting Sound Bank:%s\n", bank_path);

                result = AudioUnitSetProperty(
                        synth,
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

            if (disk_stream) {
                auto value = static_cast<UInt32>(disk_stream);
                result = AudioUnitSetProperty(
                        synth,
                        kMusicDeviceProperty_StreamFromDisk,
                        kAudioUnitScope_Global,
                        0,
                        &value,
                        sizeof(value)
                );
                check_error(result, "AudioUnitSetProperty: kMusicDeviceProperty_StreamFromDisk");
            }

            if (output_file_path != "") {
                // need to tell synth that is going to render a file.
                auto value = UInt32{1};
                result = AudioUnitSetProperty(
                        synth,
                        kAudioUnitProperty_OfflineRender,
                        kAudioUnitScope_Global,
                        0,
                        &value,
                        sizeof(value)
                );
                check_error(result, "AudioUnitSetProperty: kAudioUnitProperty_OfflineRender");
            }

            result = set_up_graph(graph, num_frames, srate, (output_file_path != ""));
            check_error(result, "SetUpGraph");

            if (should_print) {
                printf("Sample Rate: %.1f \n", srate);
                printf("Disk Streaming is enabled: %c\n", (disk_stream ? 'T' : 'F'));
            }

            result = AUGraphInitialize(graph);
            check_error(result, "AUGraphInitialize");

            if (should_print) {
                CAShow(graph);
            }
        }

        auto player = static_cast<MusicPlayer>(nullptr);
        result = NewMusicPlayer(&player);
        check_error(result, "NewMusicPlayer");

        result = MusicPlayerSetSequence(player, sequence);
        check_error(result, "MusicPlayerSetSequence");

        // figure out sequence length
        auto num_tracks = UInt32{};

        result = MusicSequenceGetTrackCount(sequence, &num_tracks);
        check_error(result, "MusicSequenceGetTrackCount");

        auto sequence_length = MusicTimeStamp{0.};

        auto should_print_tracks = should_print && !track_set.empty();
        if (should_print_tracks) {
            printf("Only playing specified tracks:\n\t");
        }

        for (auto i = static_cast<UInt32>(0); i < num_tracks; ++i) {
            auto track = static_cast<MusicTrack>(nullptr);
            auto track_length = MusicTimeStamp{};
            auto prop_size = static_cast<UInt32>(sizeof(MusicTimeStamp));

            result = MusicSequenceGetIndTrack(sequence, i, &track);
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

            if (!track_set.empty() && (track_set.find(i) == track_set.end())) {
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

        result = MusicPlayerSetTime(player, start_time);
        check_error(result, "MusicPlayerSetTime");

        result = MusicPlayerPreroll(player);
        check_error(result, "MusicPlayerPreroll");

        if (should_print) {
            printf("Ready to play: %s, %.2f beats long\n\t<Enter> to continue: ", file_path.c_str(), sequence_length);
            getc(stdin);
        }

        globals::start_running_time = CAHostTimeBase::GetTheCurrentTime();

/*		if (wait_at_end && graph)
			AUGraphStart(graph);
*/
        result = MusicPlayerStart(player);
        check_error(result, "MusicPlayerStart");

        if (output_file_path != "") {
            write_output_file(
                    output_file_path.c_str(),
                    data_format,
                    srate,
                    sequence_length,
                    should_print,
                    graph,
                    num_frames,
                    player
            );
        } else {
            play_loop(player, graph, sequence_length, should_print, wait_at_end);
        }

        result = MusicPlayerStop(player);
        check_error(result, "MusicPlayerStop");
        if (should_print) { printf("finished playing\n"); }

/*		if (wait_at_end) {
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, 10, false);
			if (graph)
				AUGraphStop(graph);
			if (should_print) printf ("disposing\n");
		}
*/
// this shows you how you should dispose of everything
        result = DisposeMusicPlayer(player);
        check_error(result, "DisposeMusicPlayer");
        result = DisposeMusicSequence(sequence);
        check_error(result, "DisposeMusicSequence");
        // don't own the graph so don't dispose it (the seq owns it as we never set it ourselves, we just got it....)
    } else {
        result = DisposeMusicSequence(sequence);
        check_error(result, "DisposeMusicSequence");
    }

    while (wait_at_end) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, false);
    }

    return 0;

    // fail:
    // if (should_print) { printf("Error = %ld\n", (long) result); }
    // return result;
}


