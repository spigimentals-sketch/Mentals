#pragma once

#if defined(_WIN32)
 #ifndef NOMINMAX
 #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

#include <atomic>
#include <array>
#include <cstdint>

//==============================================================================
// Cross-process "mix blackboard" for masking-aware AI Assist: every
// Multimode EQ instance loaded anywhere on the machine (any track, any
// host, even across separate DAW processes if the host sandboxes plugins)
// publishes its own track's coarse per-band energy profile into a shared
// slot periodically, and can read every OTHER instance's published profile
// on demand. This is what lets AI Assist's resonance-cut suggestions
// notice "something else already occupies this frequency" and cut harder
// there -- something no single-track analysis can see.
//
// Same design as Stereo Shaper's MixRegistry (see that file for the fuller
// cross-platform-shared-memory rationale) -- a named OS-level shared-
// memory mapping, Win32 file mapping on Windows, POSIX shm_open()/mmap()
// on macOS/Linux -- but a distinct name and struct layout so the two
// plugins' registries never collide with each other.
//
// Resolution note: this publishes a moderate-resolution (24-band, log-
// spaced 20Hz-20kHz) profile rather than a full spectrum, to keep the
// shared memory and per-block publish cost small. Training used exact
// per-bin occupancy measured from real, genuinely simultaneous stems (see
// Models/README.md); this live cross-instance read is necessarily a
// coarser approximation of that, interpolated between the two nearest
// published bands for an arbitrary frequency -- the same kind of live-vs-
// training-time gap Stereo Shaper's own MixRegistry context has relative
// to its training data.
//==============================================================================
class EqMixRegistry
{
public:
    static constexpr int numBands = 24;
    static constexpr int maxSlots = 32;

    static float bandCentreHz (int bandIndex) noexcept;

    struct Slot
    {
        std::atomic<uint64_t> ownerId { 0 };      // 0 == free
        std::atomic<int64_t>  lastUpdateMs { 0 };
        std::atomic<float>    bandDb[numBands] {}; // absolute dB energy per band
    };

    struct SharedMemory
    {
        Slot slots[maxSlots];
    };

    EqMixRegistry();
    ~EqMixRegistry();

    // Real-time safe: no syscalls beyond writing to already-mapped pages.
    // Called periodically from processBlock() (not every block -- see
    // MultiModeEQAudioProcessor), with an absolute-dB band profile derived
    // from the same spectrum the analyser/EQ Match already maintain.
    void publish (const std::array<float, numBands>& bandDb) noexcept;

    // Not real-time safe (harmless syscalls to read the clock) -- only
    // ever called from the message thread, when AI Assist runs.
    struct AggregateBands
    {
        std::array<float, numBands> levelDb {};
        int othersCount = 0;
    };
    AggregateBands computeAggregate (int64_t stalenessMs = 3000) const;

    // Log-interpolates between the two nearest published bands in an
    // AggregateBands result to estimate the level at an arbitrary
    // frequency -- what masking-pressure-at-a-resonance-peak actually
    // needs, rather than a fixed macro-band reading.
    static float levelAtFrequency (const AggregateBands& aggregate, float freqHz) noexcept;

private:
    bool claimSlot();

#if defined(_WIN32)
    HANDLE mappingHandle = nullptr;
#else
    int mappingFd = -1;
#endif
    SharedMemory* memory = nullptr;
    int ownSlotIndex = -1;
    uint64_t ownerId = 0;

    EqMixRegistry (const EqMixRegistry&) = delete;
    EqMixRegistry& operator= (const EqMixRegistry&) = delete;
};
