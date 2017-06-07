#ifndef CORE_MIDI_GEN2_AU_GRAPH_MANAGER_H
#define CORE_MIDI_GEN2_AU_GRAPH_MANAGER_H

#include <AudioToolbox/AudioToolbox.h>

#include "Arg_parser.h"

class Au_graph_manager {
private:
    AUGraph _graph = nullptr; // non-owning -> need to handle
    Arg_parser &_arg_parser;

public:
    Au_graph_manager(Arg_parser &arg_parser) : _arg_parser{arg_parser} {}

    ~Au_graph_manager() = default;

    AUGraph &get_graph() { return _graph; }

    void init();

    void init_sequence(MusicSequence &sequence, AudioUnit &synth);

private:
    void _set_up_graph();

    int _setup_output_unit(
            AudioUnit &output_unit,
            AudioUnit &current_unit,
            AudioComponentDescription &desc,
            AUNode &node,
            AUNode &output_node,
            int index
    );

    void _get_synth_from_au_graph(AudioUnit &synth);
};

#endif //CORE_MIDI_GEN2_AU_GRAPH_MANAGER_H
