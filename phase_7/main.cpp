#include "Core_midi_gen.h"

int main(int argc, char *argv[])
{
    auto arg_parser = Arg_parser{argc, argv};
    auto core_midi_gen = Core_midi_gen{arg_parser};
    core_midi_gen.run();
    return 0;
}
