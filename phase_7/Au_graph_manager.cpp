#include <CAHostTimeBase.h>

#include "Au_graph_manager.h"

#include "globals.h"
#include "util.h"

void Au_graph_manager::init()
{
    _set_up_graph();

    if (_arg_parser.should_print) {
        printf("Sample Rate: %.1f \n", _arg_parser.srate);
        printf("Disk Streaming is enabled: %c\n", (_arg_parser.disk_stream ? 'T' : 'F'));
    }

    auto result = AUGraphInitialize(_graph);
    check_error(result, "AUGraphInitialize");

    if (_arg_parser.should_print) {
        CAShow(_graph);
    }
}

void Au_graph_manager::init_sequence(MusicSequence &sequence, AudioUnit &synth)
{
    auto result = MusicSequenceGetAUGraph(sequence, &_graph);
    check_error(result, "MusicSequenceGetAUGraph");

    result = AUGraphOpen(_graph);
    check_error(result, "AUGraphOpen");

    _get_synth_from_au_graph(synth);
}

int Au_graph_manager::_setup_output_unit(
        AudioUnit &output_unit,
        AudioUnit &current_unit,
        AudioComponentDescription &desc,
        AUNode &node,
        AUNode &output_node,
        int index
)
{
    if (output_unit == nullptr) {
        output_unit = current_unit;

        auto result = AUGraphNodeInfo(_graph, node, 0, &output_unit);
        check_error(result, "AUGraphNodeInfo");

        if (_arg_parser.output_file_path == "") {
            _set_properties_to_render_to_device(output_unit);
        } else {
            _set_properties_to_render_offline(output_unit, current_unit, desc, node, output_node);
        }
        // reset loop
        return -1;
    }
    return index;
}

void Au_graph_manager::_reconnect_output_if_offline(
        AudioUnit &output_unit,
        AudioUnit &current_unit,
        AudioComponentDescription &desc,
        AUNode &node,
        AUNode &output_node
)
{
    if (output_unit) {
        // reconnect up to the output unit if we're offline
        if (_arg_parser.output_file_path != "" && desc.componentType != kAudioUnitType_MusicDevice) {
            auto result = AUGraphConnectNodeInput(_graph, node, 0, output_node, 0);
            check_error(result, "AUGraphConnectNodeInput");
        }

        auto result = AudioUnitSetProperty(
                current_unit,
                kAudioUnitProperty_SampleRate,
                kAudioUnitScope_Output,
                0,
                &_arg_parser.srate,
                sizeof(_arg_parser.srate)
        );
        check_error(result, "AudioUnitSetProperty: kAudioUnitProperty_SampleRate");
    }
}

int Au_graph_manager::_init_node(int node_num, AudioUnit &output_unit, AUNode &output_node)
{
    auto node = AUNode{};
    auto result = AUGraphGetIndNode(_graph, static_cast<UInt32>(node_num), &node);
    check_error(result, "AUGraphGetIndNode");

    auto desc = AudioComponentDescription{};
    auto unit = static_cast<AudioUnit>(nullptr);
    result = AUGraphNodeInfo(_graph, node, &desc, &unit);
    check_error(result, "AUGraphNodeInfo");

    if (desc.componentType == kAudioUnitType_Output) {
        node_num = _setup_output_unit(output_unit, unit, desc, node, output_node, node_num);
    } else {
        _reconnect_output_if_offline(output_unit, unit, desc, node, output_node);
    }

    result = AudioUnitSetProperty(
            unit,
            kAudioUnitProperty_MaximumFramesPerSlice,
            kAudioUnitScope_Global,
            0,
            &_arg_parser.num_frames,
            sizeof(_arg_parser.num_frames)
    );
    check_error(result, "AudioUnitSetProperty: kAudioUnitProperty_MaximumFramesPerSlice");

    return node_num;
}

void Au_graph_manager::_set_up_graph()
{
    auto output_unit = static_cast<AudioUnit>(nullptr);
    auto output_node = AUNode{};

    // the frame size is the I/O size to the device
    // the device is going to run at a sample rate it is set at
    // so, when we set this, we also have to set the max frames for the graph nodes
    auto node_count = UInt32{};
    auto result = AUGraphGetNodeCount(_graph, &node_count);
    check_error(result, "AUGraphGetNodeCount");

    for (auto i = 0; i < node_count; ++i) {
        i = _init_node(i, output_unit, output_node);
    }
}

void Au_graph_manager::_set_properties_to_render_to_device(AudioUnit output_unit)
{
    // these two properties are only applicable if its a device we're playing too
    auto result = AudioUnitSetProperty(
            output_unit,
            kAudioDevicePropertyBufferFrameSize,
            kAudioUnitScope_Output,
            0,
            &_arg_parser.num_frames,
            sizeof(_arg_parser.num_frames)
    );
    check_error(result, "AudioUnitSetProperty: kAudioDevicePropertyBufferFrameSize");

    // not part of public API
    // changed from private top-level to lambda
    auto overload_listener_proc = [](
            void */* inRefCon */,
            AudioUnit /* ci */,
            AudioUnitPropertyID /* inID */,
            AudioUnitScope /* inScope */,
            AudioUnitElement /* inElement */
    ) {
        ++globals::did_overload;
        globals::overload_time = CAHostTimeBase::GetTheCurrentTime();
    };

    result = AudioUnitAddPropertyListener(
            output_unit,
            kAudioDeviceProcessorOverload,
            overload_listener_proc, 0
    );
    check_error(result, "AudioUnitAddPropertyListener: kAudioDeviceProcessorOverload");

    // if we're rendering to the device, then we render at its sample rate
    auto size = static_cast<UInt32>(sizeof(_arg_parser.srate));
    result = AudioUnitGetProperty(
            output_unit,
            kAudioUnitProperty_SampleRate,
            kAudioUnitScope_Output,
            0,
            &_arg_parser.srate,
            &size
    );
    check_error(result, "AudioUnitGetProperty: kAudioUnitProperty_SampleRate");
}

void Au_graph_manager::_set_properties_to_render_offline(
        AudioUnit &output_unit,
        AudioUnit &current_unit,
        AudioComponentDescription &desc,
        AUNode &node,
        AUNode &output_node
)
{
    // remove device output node and add generic output
    auto result = AUGraphRemoveNode(_graph, node);
    check_error(result, "AUGraphRemoveNode");

    desc.componentSubType = kAudioUnitSubType_GenericOutput;
    result = AUGraphAddNode(_graph, &desc, &node);
    check_error(result, "AUGraphAddNode");

    result = AUGraphNodeInfo(_graph, node, nullptr, &current_unit);
    check_error(result, "AUGraphNodeInfo");
    output_unit = current_unit;
    output_node = node;

    // we render the output offline at the desired sample rate
    result = AudioUnitSetProperty(
            output_unit,
            kAudioUnitProperty_SampleRate,
            kAudioUnitScope_Output,
            0,
            &_arg_parser.srate,
            sizeof(_arg_parser.srate)
    );
    check_error(result, "AudioUnitSetProperty: kAudioUnitProperty_SampleRate");
}

void Au_graph_manager::_get_synth_from_au_graph(AudioUnit &synth)
{
    auto node_count = UInt32{};
    auto result = AUGraphGetNodeCount(_graph, &node_count);
    check_error(result, "AUGraphGetNodeCount");

    for (auto i = static_cast<UInt32>(0); i < node_count; ++i) {
        auto node = AUNode{};
        result = AUGraphGetIndNode(_graph, i, &node);
        check_error(result, "AUGraphGetIndNode");

        auto desc = AudioComponentDescription{};
        result = AUGraphNodeInfo(_graph, node, &desc, 0);
        check_error(result, "AUGraphNodeInfo");

        if (desc.componentType == kAudioUnitType_MusicDevice) {
            result = AUGraphNodeInfo(_graph, node, 0, &synth);
            check_error(result, "AUGraphNodeInfo");
        }
    }
}
