#include <CoreFoundation/CoreFoundation.h>
#include <CoreAudio/CoreAudioTypes.h>

#include <set>
#include <vector>

#include "CAAudioFileFormats.h"
#include "CAHostTimeBase.h"

#include "Arg_parser.h"

int main(int argc, char *argv[])
{
    auto ap = Arg_parser(argc, argv);

    auto sequence = static_cast<MusicSequence>(nullptr);
    auto result = load_smf(ap.file_path, sequence, ap.load_flags);
    check_error(result, "load_smf");

    if (ap.should_print) {
        CAShow(sequence);
    }

    if (ap.should_play) {
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

        if (ap.should_use_midi_endpoint) {
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
            if (ap.should_set_bank) {
                auto sound_bank_url = CFURLCreateFromFileSystemRepresentation(
                        kCFAllocatorDefault,
                        reinterpret_cast<const UInt8 *>(ap.bank_path.c_str()), // unsafe
                        ap.bank_path.size(),
                        false
                );

                printf("Setting Sound Bank:%s\n", ap.bank_path.c_str());

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

            if (ap.disk_stream) {
                auto value = static_cast<UInt32>(ap.disk_stream);
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

            if (ap.output_file_path != "") {
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

            result = set_up_graph(graph, ap.num_frames, ap.srate, (ap.output_file_path != ""));
            check_error(result, "SetUpGraph");

            if (ap.should_print) {
                printf("Sample Rate: %.1f \n", ap.srate);
                printf("Disk Streaming is enabled: %c\n", (ap.disk_stream ? 'T' : 'F'));
            }

            result = AUGraphInitialize(graph);
            check_error(result, "AUGraphInitialize");

            if (ap.should_print) {
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

        auto should_print_tracks = ap.should_print && !ap.track_set.empty();
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

            if (!ap.track_set.empty() && (ap.track_set.find(i) == ap.track_set.end())) {
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

        result = MusicPlayerSetTime(player, ap.start_time);
        check_error(result, "MusicPlayerSetTime");

        result = MusicPlayerPreroll(player);
        check_error(result, "MusicPlayerPreroll");

        if (ap.should_print) {
            printf("Ready to play: %s, %.2f beats long\n\t<Enter> to continue: ", ap.file_path.c_str(), sequence_length
            );
            getc(stdin);
        }

        globals::start_running_time = CAHostTimeBase::GetTheCurrentTime();

/*		if (wait_at_end && graph)
			AUGraphStart(graph);
*/
        result = MusicPlayerStart(player);
        check_error(result, "MusicPlayerStart");

        if (ap.output_file_path != "") {
            write_output_file(
                    ap.output_file_path.c_str(),
                    ap.data_format,
                    ap.srate,
                    sequence_length,
                    ap.should_print,
                    graph,
                    ap.num_frames,
                    player
            );
        } else {
            play_loop(player, graph, sequence_length, ap.should_print, ap.wait_at_end);
        }

        result = MusicPlayerStop(player);
        check_error(result, "MusicPlayerStop");
        if (ap.should_print) { printf("finished playing\n"); }

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

    while (ap.wait_at_end) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, false);
    }

    return 0;

    // fail:
    // if (should_print) { printf("Error = %ld\n", (long) result); }
    // return result;
}


