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
// Cross-process "mix blackboard": every Stereo Shaper instance loaded
// anywhere on the machine (any track, any host, even across separate DAW
// processes if the host sandboxes plugins) publishes its own track's audio
// fingerprint and current placement into a shared slot every block, and can
// read every OTHER instance's published slot on demand. This is what makes
// "AI Placement" mix-aware without a separate master-bus listener plugin:
// there's nothing to insert on the master bus, and no double-counting risk
// from analysing a bus that already includes the very track being placed --
// each track simply reports itself, and reads everyone else's reports.
//
// Backed by a named OS-level shared-memory mapping (not a JUCE abstraction
// -- JUCE has no cross-process shared-memory primitive): a Win32 file
// mapping on Windows, POSIX shm_open()/mmap() on macOS/Linux -- so it works
// whether the host loads every plugin in-process or sandboxes each one in
// its own process.
// Every field is a lock-free std::atomic living directly in the mapped
// pages; on x86-64 with MSVC, atomic<float>/atomic<int32_t>/atomic<int64_t>
// are guaranteed lock-free, so this is safe to touch from the audio thread
// with no risk of one process blocking on another's page fault or crash --
// at worst a reader sees a slightly-stale value, never a torn or invalid
// one, and a stale slot (owner process died without cleaning up) is simply
// aged out by timestamp rather than relying on the writer's destructor.
//==============================================================================
class MixRegistry
{
public:
    static constexpr int numOwnFeatures = 6;
    static constexpr int maxSlots = 32;

    // Named indices into the numOwnFeatures-sized array below -- used
    // identically by the processor (writing) and by the Python training
    // script's feature extraction (so the trained model's input order
    // matches what gets published at runtime).
    enum FeatureIndex
    {
        featureLowRatio = 0,
        featureMidRatio,
        featureHighRatio,
        featureCrestFactorDb,
        featureRmsDb,
        featureInputCorrelation
    };

    struct Slot
    {
        std::atomic<uint64_t> ownerId { 0 };      // 0 == free
        std::atomic<int64_t>  lastUpdateMs { 0 };
        std::atomic<float>    features[numOwnFeatures] {};
        std::atomic<float>    rotationDeg { 0.0f };
        std::atomic<float>    widthPercent { 100.0f };
    };

    struct SharedMemory
    {
        Slot slots[maxSlots];
    };

    // Everything the model needs to know about "everyone else": an
    // energy-weighted occupancy histogram across three pan regions (so the
    // model can see which part of the stereo field is already crowded) plus
    // the average spectral-band ratios of what else is playing (so it can
    // tell "there's already a lot of low end going on" apart from "there's
    // already something centred").
    struct AggregateContext
    {
        float leftOccupancy = 0.0f, centreOccupancy = 0.0f, rightOccupancy = 0.0f;
        float othersAvgLowRatio = 0.0f, othersAvgMidRatio = 0.0f, othersAvgHighRatio = 0.0f;
        int othersCount = 0;
    };

    MixRegistry();
    ~MixRegistry();

    // Real-time safe: no syscalls beyond writing to already-mapped pages.
    // Called once per block from processBlock().
    void publish (const std::array<float, numOwnFeatures>& ownFeatures, float rotationDeg, float widthPercent) noexcept;

    // Not real-time safe (harmless syscalls to read the clock) -- only ever
    // called from the message thread, when the user presses AI Placement.
    AggregateContext computeContext (int64_t stalenessMs = 3000) const;

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

    MixRegistry (const MixRegistry&) = delete;
    MixRegistry& operator= (const MixRegistry&) = delete;
};
