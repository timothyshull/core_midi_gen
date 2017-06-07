#include <AUOutputBL.h>
#include <thread>

#include "Core_midi_gen.h"

Core_midi_gen::Core_midi_gen(Arg_parser &arg_parser)
        : _arg_parser(arg_parser),
          _graph_manager{_arg_parser},
          _track_set{_arg_parser.track_set} {}

void Core_midi_gen::run()
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

CAStreamBasicDescription Core_midi_gen::_gen_basic_description(AudioFileTypeID &dest_file_type)
{
    auto output_format = CAStreamBasicDescription{};

    output_format.mSampleRate = _arg_parser.srate;
    output_format.mFormatID = _arg_parser.data_format;
    output_format.mChannelsPerFrame = 2;

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

    return output_format;
}

ExtAudioFileRef Core_midi_gen::_prepare_outfile_for_writing()
{
    auto dest_file_type = AudioFileTypeID{};
    auto output_format = _gen_basic_description(dest_file_type);

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
    check_error(result, "ExtAudioFileCreateWithURL");

    if (url) {
        CFRelease(url);
    }
    return outfile;
}

AudioUnit Core_midi_gen::_prepare_output_au_for_writing()
{
    auto output_unit = static_cast<AudioUnit>(nullptr);
    auto node_count = UInt32{};
    auto result = AUGraphGetNodeCount(_graph_manager.get_graph(), &node_count);
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

    return output_unit;
}

void Core_midi_gen::_write_buffer_to_outfile(
        MusicTimeStamp sequence_length,
        const CAStreamBasicDescription &client_format,
        const ExtAudioFileRef outfile,
        AudioUnit output_unit
)
{
    auto current_time = MusicTimeStamp{};
    AUOutputBL output_buffer{client_format, _arg_parser.num_frames};
    auto timestamp = AudioTimeStamp{0, 0, 0, 0, 0, kAudioTimeStampSampleTimeValid, 0};

    auto i = 0;
    auto num_times_for_10_secs = static_cast<int>(10. / (_arg_parser.num_frames / _arg_parser.srate));
    do {
        output_buffer.Prepare();
        auto action_flags = AudioUnitRenderActionFlags{0};

        auto result = AudioUnitRender(
                output_unit,
                &action_flags,
                &timestamp,
                0,
                _arg_parser.num_frames,
                output_buffer.ABL()
        );
        check_error(result, "AudioUnitRender");

        timestamp.mSampleTime += _arg_parser.num_frames;

        result = ExtAudioFileWrite(outfile, _arg_parser.num_frames, output_buffer.ABL());
        check_error(result, "ExtAudioFileWrite");

        result = MusicPlayerGetTime(_player, &current_time);
        check_error(result, "MusicPlayerGetTime");

        if (_arg_parser.should_print && (++i % num_times_for_10_secs == 0)) {
            printf("current time: %6.2f beats\n", current_time);
        }
    } while (current_time < sequence_length);
}

void Core_midi_gen::_write_output_file(MusicTimeStamp sequence_length)
{
    auto outfile = _prepare_outfile_for_writing();
    auto output_unit = _prepare_output_au_for_writing();

    {
        auto client_format = CAStreamBasicDescription{};
        auto size = static_cast<UInt32>(sizeof(client_format));
        auto result = AudioUnitGetProperty(
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

        _write_buffer_to_outfile(sequence_length, client_format, outfile, output_unit);
    }

    ExtAudioFileDispose(outfile);
}

void Core_midi_gen::_print_overloads()
{
    // TODO: may be able to replace with standard library
    printf("* * * * * %lu Overloads detected on device playing audio\n",
           static_cast<unsigned long>(globals::did_overload));
    globals::overload_time = CAHostTimeBase::ConvertToNanos(
            globals::overload_time - globals::start_running_time
    );
    printf("\tSeconds after start = %lf\n", globals::overload_time / 1000000000.);
    globals::did_overload = 0;
}

void Core_midi_gen::_print_load(const MusicTimeStamp& time)
{
    printf("current time: %6.2f beats", time);
    if (_graph_manager.get_graph()) {
        auto load = Float32{};
        auto result = AUGraphGetCPULoad(_graph_manager.get_graph(), &load);
        check_error(result, "AUGraphGetCPULoad");
        printf(", CPU load = %.2f%%\n", (load * 100.));
    } else {
        printf("\n");
    } //no cpu load on AU_graph - its not running - if just playing out to MIDI
}

// initialize static variable
const std::chrono::seconds Core_midi_gen::_loop_sleep_dur = std::chrono::seconds{2};

void Core_midi_gen::_print_info_while_playing(MusicTimeStamp sequence_length)
{
    auto wait_counter = 0;
    while (true) {
        // prefer standard library over Posix usleep
        std::this_thread::sleep_for(_loop_sleep_dur);

        if (globals::did_overload) {
            _print_overloads();
        }

        if (_arg_parser.wait_at_end && ++wait_counter > 10) { break; }

        auto time = MusicTimeStamp{};
        auto result = MusicPlayerGetTime(_player, &time);
        check_error(result, "MusicPlayerGetTime");

        if (_arg_parser.should_print) {
            _print_load(time);
        }

        if (time >= sequence_length) { break; }
    }
}

void Core_midi_gen::_init_sequence()
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
}

void Core_midi_gen::_init_single_track(UInt32 track_num, MusicTimeStamp &sequence_length, bool should_print_tracks)
{
    auto track = static_cast<MusicTrack>(nullptr);
    auto track_length = MusicTimeStamp{};
    auto prop_size = static_cast<UInt32>(sizeof(MusicTimeStamp));

    auto result = MusicSequenceGetIndTrack(_sequence, track_num, &track);
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

    if (_arg_parser.has_track_num(track_num)) {
        auto mute = Boolean{true};
        result = MusicTrackSetProperty(track, kSequenceTrackProperty_MuteStatus, &mute, sizeof(mute));
        check_error(result, "MusicTrackSetProperty: kSequenceTrackProperty_MuteStatus");
    } else if (should_print_tracks) {
        printf("%d, ", static_cast<int>(track_num + 1));
    }
}

void Core_midi_gen::_init_tracks(MusicTimeStamp &sequence_length)
{
    // figure out sequence length
    auto num_tracks = UInt32{};

    auto result = MusicSequenceGetTrackCount(_sequence, &num_tracks);
    check_error(result, "MusicSequenceGetTrackCount");

    auto should_print_tracks = _arg_parser.should_print_tracks();
    if (should_print_tracks) {
        printf("Only playing specified tracks:\n\t");
    }

    for (auto i = static_cast<UInt32>(0); i < num_tracks; ++i) {
        _init_single_track(i, sequence_length, should_print_tracks);
    }

    if (should_print_tracks) {
        printf("\n");
    }
}

void Core_midi_gen::_play_sequence()
{
    _init_sequence();

    auto sequence_length = MusicTimeStamp{0.};
    _init_tracks(sequence_length);
    // add 8 beats on the end for the reverb/long releases to tail off
    sequence_length += 8;

    auto result = MusicPlayerSetTime(_player, _arg_parser.start_time);
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
        _print_info_while_playing(sequence_length);
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
