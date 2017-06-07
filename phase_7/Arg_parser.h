#ifndef CORE_MIDI_GEN2_ARG_PARSER_H
#define CORE_MIDI_GEN2_ARG_PARSER_H

#include <string>
#include <vector>
#include <set>

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

    bool should_print_tracks() const { return should_print && !track_set.empty(); }

    bool has_track_num(UInt32 track_num) { return !track_set.empty() && (track_set.find(track_num) == track_set.end()); }

private:
    void _set_args(int argc, char *argv[]);

    void _parse_args();

    static void _malformed_input();

    static void _check_file_path();

    void _check_midi_endpoint();
};

#endif //CORE_MIDI_GEN2_ARG_PARSER_H
