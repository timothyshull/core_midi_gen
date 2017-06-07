#ifndef CORE_MIDI_GEN2_CORE_MIDI_GEN_H
#define CORE_MIDI_GEN2_CORE_MIDI_GEN_H

#include <AudioToolbox/AudioToolbox.h>

#include <string>
#include <map>

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

namespace globals {
    const std::map<const std::string, const std::string> cmd_strings{
            {"bank_cmd",       "[-b /Path/To/Sound/Bank.dls]\n\t"},
            {"smf_chan_cmd",   "[-c] Will Parse MIDI file into channels\n\t"},
            {"disk_stream",    "[-d] Turns disk streaming on\n\t"},
            {"midi_cmd",       "[-e] Use a MIDI Endpoint\n\t"},
            {"file_cmd",       "[-f /Path/To/File.<EXT FOR FORMAT> 'data' srate] Create a stereo file where\n\t"},
            {"file_cmd_1",     "\t\t 'data' is the data format (lpcm or a compressed type, like 'aac ')\n\t"},
            {"file_cmd_2",     "\t\t srate is the sample rate\n\t"},
            {"num_frames_cmd", "[-i io Sample Size] default is 512\n\t"},
            {"no_print_cmd",   "[-n] Don't print\n\t"},
            {"play_cmd",       "[-p] Play the Sequence\n\t"},
            {"start_time_cmd", "[-s startTime-Beats]\n\t"},
            {"track_cmd",      "[-t trackIndex] Play specified track(s), e.g. -t 1 -t 2...(this is a one based index)\n\t"},
            {"wait_cmd",       "[-w] Play for 10 seconds, then dispose all objects and wait at end\n\t"},
            {"src_file_cmd",   "/Path/To/File.mid"},
            {"usage_str",      "Usage: PlaySequence\n\t"}
    };

    const auto usage_string = cmd_strings.at("usage_str") +
                              cmd_strings.at("bank_cmd") +
                              cmd_strings.at("smf_chan_cmd") +
                              cmd_strings.at("disk_stream") +
                              cmd_strings.at("midi_cmd") +
                              cmd_strings.at("file_cmd") +
                              cmd_strings.at("file_cmd_1") +
                              cmd_strings.at("file_cmd_2") +
                              cmd_strings.at("num_frames_cmd") +
                              cmd_strings.at("no_print_cmd") +
                              cmd_strings.at("play_cmd") +
                              cmd_strings.at("start_time_cmd") +
                              cmd_strings.at("track_cmd") +
                              cmd_strings.at("wait_cmd") +
                              cmd_strings.at("src_file_cmd");

    static auto did_overload = UInt32{0};
    static auto overload_time = UInt64{0};
    static auto start_running_time = UInt64{};
    static auto max_cpu_load = Float32{.8};
};

#endif //CORE_MIDI_GEN2_CORE_MIDI_GEN_H
