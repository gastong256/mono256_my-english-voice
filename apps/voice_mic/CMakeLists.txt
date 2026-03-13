add_executable(mev_voice_mic main.cpp)
target_link_libraries(mev_voice_mic PRIVATE mev)

mev_set_warnings(mev_voice_mic)
mev_enable_sanitizers(mev_voice_mic)
