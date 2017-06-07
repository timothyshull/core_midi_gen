#include <string>
#include <vector>

#include <CAAudioFileFormats.h>

#include "Arg_parser.h"

#include "globals.h"
#include "util.h"

Arg_parser::Arg_parser(int argc, char *argv[])
        : args(static_cast<std::vector<std::string>::size_type>(argc)),
          argc{argc}
{
    if (argc == 1) {
        _malformed_input();
    }
    _set_args(argc, argv);
    _parse_args();
    _check_file_path();
    _check_midi_endpoint();
}

void Arg_parser::_set_args(int argc, char **argv)
{
    for (auto i = 0; i < argc; ++i) {
        args[i] = argv[i];
    }
}

void Arg_parser::_parse_args()
{
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
                _malformed_input();
            }
            bank_path = args[i];
        } else if (args[i] == "-n") {
            should_print = false;
        } else if ((file_path == "") && (args[i][0] == '/' || args[i][0] == '~')) {
            file_path = args[i];
        } else if (args[i] == "-s") {
            if (++i == argc) {
                _malformed_input();
            }
            start_time = lexical_cast<decltype(start_time), decltype(args[i])>(args[i]);
        } else if (args[i] == "-t") {
            if (++i == argc) {
                _malformed_input();
            }
            auto index = lexical_cast<int, decltype(args[i])>(args[i]);
            track_set.insert(--index);
        } else if (args[i] == "-e") {
            should_use_midi_endpoint = true;
        } else if (args[i] == "-c") {
            load_flags = kMusicSequenceLoadSMF_ChannelsToTracks;
        } else if (args[i] == "-i") {
            if (++i == argc) {
                _malformed_input();
            }
            num_frames = lexical_cast<decltype(num_frames), decltype(args[i])>(args[i]);
        } else if (args[i] == "-f") {
            if (i + 3 >= argc) {
                _malformed_input();
            }
            output_file_path = args[++i];
            StrToOSType(args[++i].c_str(), data_format);
            ++i;
            srate = lexical_cast<decltype(srate), decltype(args[i])>(args[i]);
        } else {
            _malformed_input();
        }
    }
}

void Arg_parser::_malformed_input()
{
    fprintf(stderr, "%s\n", globals::usage_string.c_str());
    exit(1);
}

void Arg_parser::_check_file_path()
{
    if (file_path == "") {
        fprintf(stderr, "You have to specify a MIDI file to print or play\n");
        fprintf(stderr, "%s\n", globals::usage_string.c_str());
        exit(1);
    }
}

void Arg_parser::_check_midi_endpoint()
{
    if (should_use_midi_endpoint && output_file_path != "") {
        printf("can't write a file when you try to play out to a MIDI Endpoint\n");
        exit(1);
    }
}