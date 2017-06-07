cmake_minimum_required(VERSION 3.8)
project(core_midi_gen2)

set(CMAKE_CXX_STANDARD 14)

add_library(util util.cpp)

include_directories(
        /Library/Developer/CoreAudio/AudioCodecs
        /Library/Developer/CoreAudio/AudioCodecs/ACPublic
        /Library/Developer/CoreAudio/AudioFile
        /Library/Developer/CoreAudio/AudioFile/AFPublic
        /Library/Developer/CoreAudio/AudioUnits
        /Library/Developer/CoreAudio/AudioUnits/AUPublic
        /Library/Developer/CoreAudio/AudioUnits/AUPublic/AUBase
        /Library/Developer/CoreAudio/AudioUnits/AUPublic/AUCarbonViewBase
        /Library/Developer/CoreAudio/AudioUnits/AUPublic/AUInstrumentBase
        /Library/Developer/CoreAudio/AudioUnits/AUPublic/AUViewBase
        /Library/Developer/CoreAudio/AudioUnits/AUPublic/OtherBases
        /Library/Developer/CoreAudio/AudioUnits/AUPublic/Utility
        /Library/Developer/CoreAudio/PublicUtility
)

add_executable(core_midi_gen2
        main.cpp
        Core_midi_gen.cpp
        Au_graph_manager.cpp
        Arg_parser.cpp
        globals.h
        /Library/Developer/CoreAudio/PublicUtility/AUOutputBL.cpp
        /Library/Developer/CoreAudio/PublicUtility/CAStreamBasicDescription.cpp
        /Library/Developer/CoreAudio/PublicUtility/CAStreamBasicDescription.cpp
        /Library/Developer/CoreAudio/PublicUtility/CAAudioFileFormats.cpp
        /Library/Developer/CoreAudio/PublicUtility/CAFilePathUtils.cpp
        /Library/Developer/CoreAudio/PublicUtility/CAHostTimeBase.cpp
        )
target_link_libraries(core_midi_gen2
        "-framework CoreFoundation -framework AudioToolbox -framework AudioUnit -framework CoreAudio -framework CoreMIDI -framework CoreServices"
        util
        )
