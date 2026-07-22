#include "EqMixRegistry.h"
#include <cmath>
#include <algorithm>
#include <chrono>

#if defined(_WIN32)
 #include <processthreadsapi.h>
#else
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <fcntl.h>
 #include <unistd.h>
#endif

namespace
{
#if defined(_WIN32)
    constexpr wchar_t mappingName[] = L"Local\\MentalsEqMixRegistry_v1";
#else
    constexpr const char* mappingName = "/MentalsEqMixRegistry_v1";
#endif

    // Monotonic milliseconds, used identically on every platform for the
    // slot-staleness timestamps (same approach as Stereo Shaper's
    // MixRegistry -- see that file for why GetTickCount64() isn't used).
    int64_t nowMs() noexcept
    {
        using namespace std::chrono;
        return (int64_t) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
    }
}

float EqMixRegistry::bandCentreHz (int bandIndex) noexcept
{
    constexpr float loHz = 20.0f, hiHz = 20000.0f;
    const float t = (float) bandIndex / (float) (numBands - 1);
    return loHz * std::pow (hiHz / loHz, t);
}

EqMixRegistry::EqMixRegistry()
{
#if defined(_WIN32)
    mappingHandle = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                         0, (DWORD) sizeof (SharedMemory), mappingName);
    if (mappingHandle == nullptr)
        return;

    memory = static_cast<SharedMemory*> (MapViewOfFile (mappingHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (SharedMemory)));
    if (memory == nullptr)
    {
        CloseHandle (mappingHandle);
        mappingHandle = nullptr;
        return;
    }
#else
    mappingFd = shm_open (mappingName, O_CREAT | O_RDWR, 0666);
    if (mappingFd < 0)
        return;

    if (ftruncate (mappingFd, (off_t) sizeof (SharedMemory)) != 0)
    {
        close (mappingFd);
        mappingFd = -1;
        return;
    }

    memory = static_cast<SharedMemory*> (mmap (nullptr, sizeof (SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, mappingFd, 0));
    if (memory == MAP_FAILED)
    {
        memory = nullptr;
        close (mappingFd);
        mappingFd = -1;
        return;
    }
#endif

    static std::atomic<uint32_t> instanceCounter { 0 };
    const uint32_t low = instanceCounter.fetch_add (1) ^ (uint32_t) reinterpret_cast<uintptr_t> (this);
#if defined(_WIN32)
    const uint64_t pid = (uint64_t) GetCurrentProcessId();
#else
    const uint64_t pid = (uint64_t) getpid();
#endif
    ownerId = (pid << 32) | (uint64_t) low;
    if (ownerId == 0)
        ownerId = 1;

    claimSlot();
}

EqMixRegistry::~EqMixRegistry()
{
    if (memory != nullptr && ownSlotIndex >= 0)
        memory->slots[ownSlotIndex].ownerId.store (0, std::memory_order_release);

#if defined(_WIN32)
    if (memory != nullptr)
        UnmapViewOfFile (memory);
    if (mappingHandle != nullptr)
        CloseHandle (mappingHandle);
#else
    if (memory != nullptr)
        munmap (memory, sizeof (SharedMemory));
    if (mappingFd >= 0)
        close (mappingFd);
    // Deliberately not shm_unlink()'d -- see MixRegistry.cpp's destructor
    // comment for why a leftover segment is harmless and preferable to
    // yanking it out from under other still-running instances.
#endif
}

bool EqMixRegistry::claimSlot()
{
    if (memory == nullptr)
        return false;

    for (int i = 0; i < maxSlots; ++i)
    {
        uint64_t expected = 0;
        if (memory->slots[i].ownerId.compare_exchange_strong (expected, ownerId))
        {
            ownSlotIndex = i;
            return true;
        }
    }
    return false; // registry full -- this instance simply won't publish/read
}

void EqMixRegistry::publish (const std::array<float, numBands>& bandDb) noexcept
{
    if (memory == nullptr || ownSlotIndex < 0)
        return;

    auto& slot = memory->slots[ownSlotIndex];
    for (int i = 0; i < numBands; ++i)
        slot.bandDb[i].store (bandDb[(size_t) i], std::memory_order_relaxed);
    slot.lastUpdateMs.store (nowMs(), std::memory_order_release);
}

EqMixRegistry::AggregateBands EqMixRegistry::computeAggregate (int64_t stalenessMs) const
{
    AggregateBands result;
    if (memory == nullptr)
        return result;

    std::array<double, numBands> sumLinear {};
    const int64_t now = nowMs();

    for (int i = 0; i < maxSlots; ++i)
    {
        if (i == ownSlotIndex)
            continue;

        const auto& slot = memory->slots[i];
        if (slot.ownerId.load (std::memory_order_acquire) == 0)
            continue;
        if (now - slot.lastUpdateMs.load (std::memory_order_acquire) > stalenessMs)
            continue; // stale -- an instance closed/crashed without releasing its slot

        for (int b = 0; b < numBands; ++b)
            sumLinear[(size_t) b] += std::pow (10.0, (double) slot.bandDb[b].load (std::memory_order_relaxed) / 10.0);
        ++result.othersCount;
    }

    if (result.othersCount > 0)
        for (int b = 0; b < numBands; ++b)
            result.levelDb[(size_t) b] = (float) (10.0 * std::log10 (sumLinear[(size_t) b] / (double) result.othersCount + 1.0e-12));
    else
        result.levelDb.fill (-100.0f);

    return result;
}

float EqMixRegistry::levelAtFrequency (const AggregateBands& aggregate, float freqHz) noexcept
{
    if (aggregate.othersCount <= 0)
        return -100.0f;

    freqHz = std::clamp (freqHz, bandCentreHz (0), bandCentreHz (numBands - 1));

    int lo = 0;
    while (lo < numBands - 2 && bandCentreHz (lo + 1) < freqHz)
        ++lo;
    const int hi = lo + 1;

    const float loFreq = bandCentreHz (lo);
    const float hiFreq = bandCentreHz (hi);
    const float t = (hiFreq > loFreq) ? (freqHz - loFreq) / (hiFreq - loFreq) : 0.0f;

    // Interpolate in the power domain (matching how the aggregate itself
    // was averaged), not a plain dB lerp.
    const double loLin = std::pow (10.0, (double) aggregate.levelDb[(size_t) lo] / 10.0);
    const double hiLin = std::pow (10.0, (double) aggregate.levelDb[(size_t) hi] / 10.0);
    const double lin = loLin + (hiLin - loLin) * (double) t;
    return (float) (10.0 * std::log10 (lin + 1.0e-12));
}
