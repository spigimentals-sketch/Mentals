# ----------------------------------------------------------------------------
# Mentals Live Host: a standalone application (not a plugin) that opens a
# multichannel audio device directly -- something only an app that owns the
# hardware can do, never a plugin hosted inside a DAW -- and runs each input
# channel through its own independent Mentals Suite instance as that
# channel's freely user-configurable insert chain (add/remove/reorder any of
# the suite's plugin types via Suite's own existing "+Add Module" editor,
# the same way a DAW mixer channel's inserts work), routing back out to the
# matching output channel. Meant to sit between a digital mixer (via Dante
# Virtual Soundcard, a MADI interface, or the mixer's own USB multichannel
# I/O, all of which present as an ordinary ASIO/WASAPI device) and the
# mixer's channel inserts.
#
# Reuses MentalsSuiteCore unmodified -- no DSP or chain-hosting logic is
# duplicated or reimplemented here.
# ----------------------------------------------------------------------------
juce_add_gui_app(MentalsLiveHost
    COMPANY_NAME "YourCompany"
    PRODUCT_NAME "Mentals Live Host"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

juce_generate_juce_header(MentalsLiveHost)

target_sources(MentalsLiveHost
    PRIVATE
        Main.cpp
        MainComponent.cpp
        ChannelStripComponent.cpp
        LiveChannelStrip.cpp
        LiveAudioEngine.cpp)

target_compile_definitions(MentalsLiveHost
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MentalsLiveHost,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MentalsLiveHost,JUCE_VERSION>")

target_link_libraries(MentalsLiveHost
    PRIVATE
        MentalsUI
        MentalsSuiteCore
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# onnxruntime is a runtime dependency of two of the modules Mentals Suite can
# chain in (Autotune's AI Assist, Stereo Shaper's AI Placement, and
# MultimodeEQ's AI Assist), pulled in transitively via MentalsSuiteCore, as a
# runtime-imported SHARED library rather than a static one -- so it needs its
# own copy sitting next to this executable, same as Mentals Suite's own
# CMakeLists.txt does for its plugin formats. Missing this causes a native
# crash (not a catchable C++ exception) the moment one of those models'
# constructor tries to create its Ort::Env, since that's a real DLL call, not
# something try/catch around std::exception can intercept.
# ----------------------------------------------------------------------------
add_custom_command(TARGET MentalsLiveHost POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:onnxruntime>"
        "$<TARGET_FILE_DIR:MentalsLiveHost>")
