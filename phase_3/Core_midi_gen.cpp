#include "Core_midi_gen.h"

#include "AUOutputBL.h"

OSStatus Core_midi_gen::load_smf(const std::string &filename, MusicSequence &sequence, MusicSequenceLoadFlags load_flags)
{
    auto result = NewMusicSequence(&sequence);
    check_error(result, "NewMusicSequence");

    auto url = CFURLCreateFromFileSystemRepresentation(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8 *>(filename.c_str()), // unsafe
            filename.size(),
            false
    );
    result = MusicSequenceFileLoad(sequence, url, kMusicSequenceFile_AnyType, load_flags);
    // check_error(result, "MusicSequenceFileLoad");

    if (url) {
        CFRelease(url);
    }
    return result;
}

OSStatus Core_midi_gen::get_synth_from_au_graph(AUGraph &in_graph, AudioUnit &out_synth)
{
    auto node_count = UInt32{};
    auto result = AUGraphGetNodeCount(in_graph, &node_count);
    check_error(result, "AUGraphGetNodeCount");

    for (auto i = static_cast<UInt32>(0); i < node_count; ++i) {
        auto node = AUNode{};
        result = AUGraphGetIndNode(in_graph, i, &node);
        check_error(result, "AUGraphGetIndNode");

        auto desc = AudioComponentDescription{};
        result = AUGraphNodeInfo(in_graph, node, &desc, 0);
        check_error(result, "AUGraphNodeInfo");

        if (desc.componentType == kAudioUnitType_MusicDevice) {
            result = AUGraphNodeInfo(in_graph, node, 0, &out_synth);
            check_error(result, "AUGraphNodeInfo");
            return noErr;
        }
    }

    // fail:        // didn't find the synth AU
    return -1;
}

// not part of public API
void overload_listener_proc(
        void */* inRefCon */,
        AudioUnit /* ci */,
        AudioUnitPropertyID /* inID */,
        AudioUnitScope /* inScope */,
        AudioUnitElement /* inElement */
)
{
    ++globals::did_overload;
    globals::overload_time = CAHostTimeBase::GetTheCurrentTime();
}

OSStatus Core_midi_gen::set_up_graph(AUGraph &in_graph, UInt32 num_frames, Float64 &out_sample_rate, bool is_offline)
{
    auto output_unit = static_cast<AudioUnit>(nullptr);
    auto output_node = AUNode{};

    // the frame size is the I/O size to the device
    // the device is going to run at a sample rate it is set at
    // so, when we set this, we also have to set the max frames for the graph nodes
    auto node_count = UInt32{};
    auto result = AUGraphGetNodeCount(in_graph, &node_count);
    check_error(result, "AUGraphGetNodeCount");

    for (auto i = 0; i < node_count; ++i) {
        auto node = AUNode{};
        result = AUGraphGetIndNode(in_graph, static_cast<UInt32>(i), &node);
        check_error(result, "AUGraphGetIndNode");

        auto desc = AudioComponentDescription{};
        auto unit = static_cast<AudioUnit>(nullptr);
        result = AUGraphNodeInfo(in_graph, node, &desc, &unit);
        check_error(result, "AUGraphNodeInfo");

        if (desc.componentType == kAudioUnitType_Output) {
            if (output_unit == nullptr) {
                output_unit = unit;
                result = AUGraphNodeInfo(in_graph, node, 0, &output_unit);
                check_error(result, "AUGraphNodeInfo");

                if (!is_offline) {
                    // these two properties are only applicable if its a device we're playing too
                    result = AudioUnitSetProperty(
                            output_unit,
                            kAudioDevicePropertyBufferFrameSize,
                            kAudioUnitScope_Output,
                            0,
                            &num_frames,
                            sizeof(num_frames)
                    );
                    check_error(result, "AudioUnitSetProperty: kAudioDevicePropertyBufferFrameSize");

                    result = AudioUnitAddPropertyListener(
                            output_unit,
                            kAudioDeviceProcessorOverload,
                            overload_listener_proc, 0
                    );
                    check_error(result, "AudioUnitAddPropertyListener: kAudioDeviceProcessorOverload");

                    // if we're rendering to the device, then we render at its sample rate
                    auto size = static_cast<UInt32>(sizeof(out_sample_rate));
                    result = AudioUnitGetProperty(
                            output_unit,
                            kAudioUnitProperty_SampleRate,
                            kAudioUnitScope_Output,
                            0,
                            &out_sample_rate,
                            &size
                    );
                    check_error(result, "AudioUnitGetProperty: kAudioUnitProperty_SampleRate");
                } else {
                    // remove device output node and add generic output
                    result = AUGraphRemoveNode(in_graph, node);
                    check_error(result, "AUGraphRemoveNode");

                    desc.componentSubType = kAudioUnitSubType_GenericOutput;
                    result = AUGraphAddNode(in_graph, &desc, &node);
                    check_error(result, "AUGraphAddNode");

                    result = AUGraphNodeInfo(in_graph, node, nullptr, &unit);
                    check_error(result, "AUGraphNodeInfo");
                    output_unit = unit;
                    output_node = node;

                    // we render the output offline at the desired sample rate
                    result = AudioUnitSetProperty(
                            output_unit,
                            kAudioUnitProperty_SampleRate,
                            kAudioUnitScope_Output, 0,
                            &out_sample_rate, sizeof(out_sample_rate)
                    );
                    check_error(result, "AudioUnitSetProperty: kAudioUnitProperty_SampleRate");
                }
                // ok, lets start the loop again now and do it all...
                i = -1;
            }
        } else {
            // we only have to do this on the output side
            // as the graph's connection mgmt will propogate this down.
            if (output_unit) {
                // reconnect up to the output unit if we're offline
                if (is_offline && desc.componentType != kAudioUnitType_MusicDevice) {
                    result = AUGraphConnectNodeInput(in_graph, node, 0, output_node, 0);
                    check_error(result, "AUGraphConnectNodeInput");
                }

                result = AudioUnitSetProperty(
                        unit,
                        kAudioUnitProperty_SampleRate,
                        kAudioUnitScope_Output,
                        0,
                        &out_sample_rate,
                        sizeof(out_sample_rate)
                );
                check_error(result, "AudioUnitSetProperty: kAudioUnitProperty_SampleRate");
            }
        }
        result = AudioUnitSetProperty(
                unit,
                kAudioUnitProperty_MaximumFramesPerSlice,
                kAudioUnitScope_Global,
                0,
                &num_frames,
                sizeof(num_frames)
        );
        check_error(result, "AudioUnitSetProperty: kAudioUnitProperty_MaximumFramesPerSlice");
    }

    // home:
    return result;
}

void Core_midi_gen::write_output_file(
        const char *output_file_path,
        OSType data_format,
        Float64 srate,
        MusicTimeStamp sequence_length,
        bool should_print,
        AUGraph in_graph,
        UInt32 num_frames,
        MusicPlayer player
)
{
    auto output_format = CAStreamBasicDescription{};
    output_format.mChannelsPerFrame = 2;
    output_format.mSampleRate = srate;
    output_format.mFormatID = data_format;

    auto dest_file_type = AudioFileTypeID{};
    CAAudioFileFormats::Instance()->InferFileFormatFromFilename(output_file_path, dest_file_type);

    if (data_format == kAudioFormatLinearPCM) {
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

    if (should_print) {
        printf("Writing to file: %s with format:\n* ", output_file_path);
        output_format.Print();
    }

    auto url = CFURLCreateFromFileSystemRepresentation(
            nullptr,
            reinterpret_cast<const UInt8 *>(output_file_path), // unsafe
            strlen(output_file_path),
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
    result = AUGraphGetNodeCount(in_graph, &node_count);
    check_error(result, "AUGraphGetNodeCount");

    for (auto i = static_cast<UInt32>(0); i < node_count; ++i) {
        auto node = AUNode{};
        result = AUGraphGetIndNode(in_graph, i, &node);
        check_error(result, "AUGraphGetIndNode");

        auto desc = AudioComponentDescription{};
        result = AUGraphNodeInfo(in_graph, node, &desc, NULL);
        check_error(result, "AUGraphNodeInfo");

        if (desc.componentType == kAudioUnitType_Output) {
            result = AUGraphNodeInfo(in_graph, node, 0, &output_unit);
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
            AUOutputBL output_buffer{client_format, num_frames};
            auto timestamp = AudioTimeStamp{};
            memset(&timestamp, 0, sizeof(AudioTimeStamp));
            timestamp.mFlags = kAudioTimeStampSampleTimeValid;
            auto i = 0;
            auto numTimesFor10Secs = static_cast<int>(10. / (num_frames / srate));
            do {
                output_buffer.Prepare();
                auto action_flags = AudioUnitRenderActionFlags{0};
                result = AudioUnitRender(output_unit, &action_flags, &timestamp, 0, num_frames, output_buffer.ABL());
                check_error(result, "AudioUnitRender");

                timestamp.mSampleTime += num_frames;

                result = ExtAudioFileWrite(outfile, num_frames, output_buffer.ABL());
                check_error(result, "ExtAudioFileWrite");

                result = MusicPlayerGetTime(player, &current_time);
                check_error(result, "MusicPlayerGetTime");
                if (should_print && (++i % numTimesFor10Secs == 0)) {
                    printf("current time: %6.2f beats\n", current_time);
                }
            } while (current_time < sequence_length);
        }
    }

// close
    ExtAudioFileDispose(outfile);

    return;
}

void Core_midi_gen::play_loop(MusicPlayer &player, AUGraph &graph, MusicTimeStamp sequence_length, bool should_print, bool wait_at_end)
{
    auto wait_counter = 0;
    while (true) {
        usleep(2 * 1000 * 1000);

        if (globals::did_overload) {
            printf("* * * * * %lu Overloads detected on device playing audio\n",
                   static_cast<unsigned long>(globals::did_overload));
            globals::overload_time = CAHostTimeBase::ConvertToNanos(
                    globals::overload_time - globals::start_running_time
            );
            printf("\tSeconds after start = %lf\n", globals::overload_time / 1000000000.);
            globals::did_overload = 0;
        }

        if (wait_at_end && ++wait_counter > 10) {
            break;
        }

        auto time = MusicTimeStamp{};
        auto result = MusicPlayerGetTime(player, &time);
        check_error(result, "MusicPlayerGetTime");

        if (should_print) {
            printf("current time: %6.2f beats", time);
            if (graph) {
                auto load = Float32{};
                result = AUGraphGetCPULoad(graph, &load);
                check_error(result, "AUGraphGetCPULoad");
                printf(", CPU load = %.2f%%\n", (load * 100.));
            } else {
                printf("\n");
            } //no cpu load on AUGraph - its not running - if just playing out to MIDI
        }

        if (time >= sequence_length) {
            break;
        }
    }
}
