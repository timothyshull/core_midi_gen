#include <AUOutputBL.h>
#include <thread>

#include "Core_midi_gen.h"

Core_midi_gen::Core_midi_gen(int argc, char **argv)
        : _arg_parser{argc, argv},
          _graph_manager{_arg_parser},
          _track_set{_arg_parser.track_set}
{
    _load_midi_file_to_sequence();

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

Core_midi_gen::~Core_midi_gen()
{
    // resource disposal
    if (_arg_parser.should_play) {
        auto result = DisposeMusicPlayer(_player);
        check_error(result, "DisposeMusicPlayer");
    }
    auto result = DisposeMusicSequence(_sequence);
    check_error(result, "DisposeMusicSequence");
    // don't own the graph so don't dispose it (the seq owns it as we never set it ourselves, we just got it....)
}

void Core_midi_gen::_load_midi_file_to_sequence()
{
    auto result = NewMusicSequence(&_sequence);
    check_error(result, "NewMusicSequence");

    auto url = CFURLCreateFromFileSystemRepresentation(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8 *>(_arg_parser.file_path.c_str()), // unsafe
            _arg_parser.file_path.size(),
            false
    );
    result = MusicSequenceFileLoad(_sequence, url, kMusicSequenceFile_AnyType, _arg_parser.load_flags);
    check_error(result, "MusicSequenceFileLoad");

    if (url) {
        CFRelease(url);
    }
}

void Core_midi_gen::_write_output_file(MusicTimeStamp sequence_length)
{
    auto output_format = CAStreamBasicDescription{};
    output_format.mChannelsPerFrame = 2;
    output_format.mSampleRate = _arg_parser.srate;
    output_format.mFormatID = _arg_parser.data_format;

    auto dest_file_type = AudioFileTypeID{};
    CAAudioFileFormats::Instance()->InferFileFormatFromFilename(_arg_parser.output_file_path.c_str(), dest_file_type);

    if (_arg_parser.data_format == kAudioFormatLinearPCM) {
        output_format.mBytesPerPacket = output_format.mChannelsPerFrame * 2;
        output_format.mFramesPerPacket = 1;
        output_format.mBytesPerFrame = output_format.mBytesPerPacket;
        output_format.mBitsPerChannel = 16;

        if (dest_file_type == kAudioFileWAVEType) {
            output_format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
        } else {
            output_format.mFormatFlags = kLinearPCMFormatFlagIsBigEndian |
                                         kLinearPCMFormatFlagIsSignedInteger |
                                         kLinearPCMFormatFlagIsPacked;
        }
    } else {
        // use AudioFormat API to fill out the rest.
        auto size = static_cast<UInt32>(sizeof(output_format));
        auto result = AudioFormatGetProperty(kAudioFormatProperty_FormatInfo, 0, nullptr, &size, &output_format);
        check_error(result, "");
    }

    if (_arg_parser.should_print) {
        printf("Writing to file: %s with format:\n* ", _arg_parser.output_file_path.c_str());
        output_format.Print();
    }

    auto url = CFURLCreateFromFileSystemRepresentation(
            nullptr,
            reinterpret_cast<const UInt8 *>(_arg_parser.output_file_path.c_str()), // unsafe
            strlen(_arg_parser.output_file_path.c_str()),
            false
    );

    // create output file, delete existing file
    auto outfile = static_cast<ExtAudioFileRef>(nullptr);
    auto result = ExtAudioFileCreateWithURL(
            url,
            dest_file_type,
            &output_format,
            nullptr,
            kAudioFileFlags_EraseFile,
            &outfile
    );

    if (url) {
        CFRelease(url);
    }

    check_error(result, "ExtAudioFileCreateWithURL");

    auto output_unit = static_cast<AudioUnit>(nullptr);
    auto node_count = UInt32{};
    result = AUGraphGetNodeCount(_graph_manager.get_graph(), &node_count);
    check_error(result, "AUGraphGetNodeCount");

    for (auto i = static_cast<UInt32>(0); i < node_count; ++i) {
        auto node = AUNode{};
        result = AUGraphGetIndNode(_graph_manager.get_graph(), i, &node);
        check_error(result, "AUGraphGetIndNode");

        auto desc = AudioComponentDescription{};
        result = AUGraphNodeInfo(_graph_manager.get_graph(), node, &desc, nullptr);
        check_error(result, "AUGraphNodeInfo");

        if (desc.componentType == kAudioUnitType_Output) {
            result = AUGraphNodeInfo(_graph_manager.get_graph(), node, 0, &output_unit);
            check_error(result, "AUGraphNodeInfo");
            break;
        }
    }

    result = (output_unit == nullptr);
    check_error(result, "output_unit == NULL");
    {
        auto client_format = CAStreamBasicDescription{};
        auto size = static_cast<UInt32>(sizeof(client_format));
        result = AudioUnitGetProperty(
                output_unit,
                kAudioUnitProperty_StreamFormat,
                kAudioUnitScope_Output,
                0,
                &client_format,
                &size
        );
        check_error(result, "AudioUnitGetProperty: kAudioUnitProperty_StreamFormat");
        size = sizeof(client_format);
        result = ExtAudioFileSetProperty(outfile, kExtAudioFileProperty_ClientDataFormat, size, &client_format);
        check_error(result, "ExtAudioFileSetProperty: kExtAudioFileProperty_ClientDataFormat");

        {
            auto current_time = MusicTimeStamp{};
            AUOutputBL output_buffer{client_format, _arg_parser.num_frames};
            auto timestamp = AudioTimeStamp{};
            memset(&timestamp, 0, sizeof(AudioTimeStamp));
            timestamp.mFlags = kAudioTimeStampSampleTimeValid;
            auto i = 0;
            auto numTimesFor10Secs = static_cast<int>(10. / (_arg_parser.num_frames / _arg_parser.srate));
            do {
                output_buffer.Prepare();
                auto action_flags = AudioUnitRenderActionFlags{0};
                result = AudioUnitRender(output_unit, &action_flags, &timestamp, 0, _arg_parser.num_frames,
                                         output_buffer.ABL());
                check_error(result, "AudioUnitRender");

                timestamp.mSampleTime += _arg_parser.num_frames;

                result = ExtAudioFileWrite(outfile, _arg_parser.num_frames, output_buffer.ABL());
                check_error(result, "ExtAudioFileWrite");

                result = MusicPlayerGetTime(_player, &current_time);
                check_error(result, "MusicPlayerGetTime");
                if (_arg_parser.should_print && (++i % numTimesFor10Secs == 0)) {
                    printf("current time: %6.2f beats\n", current_time);
                }
            } while (current_time < sequence_length);
        }
    }

    ExtAudioFileDispose(outfile);
}

// initialize static variable
const std::chrono::seconds Core_midi_gen::_loop_sleep_dur = std::chrono::seconds{2};

void Core_midi_gen::_play_loop(MusicTimeStamp sequence_length)
{
    auto wait_counter = 0;
    while (true) {
        // prefer standard library over Posix
        std::this_thread::sleep_for(_loop_sleep_dur);

        if (globals::did_overload) {
            // TODO: may be able to replace with standard library
            printf("* * * * * %lu Overloads detected on device playing audio\n",
                   static_cast<unsigned long>(globals::did_overload));
            globals::overload_time = CAHostTimeBase::ConvertToNanos(
                    globals::overload_time - globals::start_running_time
            );
            printf("\tSeconds after start = %lf\n", globals::overload_time / 1000000000.);
            globals::did_overload = 0;
        }

        if (_arg_parser.wait_at_end && ++wait_counter > 10) {
            break;
        }

        auto time = MusicTimeStamp{};
        auto result = MusicPlayerGetTime(_player, &time);
        check_error(result, "MusicPlayerGetTime");

        if (_arg_parser.should_print) {
            printf("current time: %6.2f beats", time);
            if (_graph_manager.get_graph()) {
                auto load = Float32{};
                result = AUGraphGetCPULoad(_graph_manager.get_graph(), &load);
                check_error(result, "AUGraphGetCPULoad");
                printf(", CPU load = %.2f%%\n", (load * 100.));
            } else {
                printf("\n");
            } //no cpu load on AU_graph - its not running - if just playing out to MIDI
        }

        if (time >= sequence_length) {
            break;
        }
    }
}

void Core_midi_gen::_play_sequence()
{
    _graph_manager.init_sequence(_sequence, _synth);

    auto result = AudioUnitSetProperty(
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
        _write_output_file(sequence_length);
    } else {
        _play_loop(sequence_length);
    }

    result = MusicPlayerStop(_player);
    check_error(result, "MusicPlayerStop");
    if (_arg_parser.should_print) { printf("finished playing\n"); }

    // moved clean-up to dtor
}

void Core_midi_gen::_setup_midi_endpoint()
{
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

void Core_midi_gen::_setup_alternate_output()
{
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

    _graph_manager.init();
}
