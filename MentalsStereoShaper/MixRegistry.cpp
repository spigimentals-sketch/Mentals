#include "MixRegistry.h"
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
    constexpr wchar_t mappingName[] = L"Local\\MentalsMixRegistry_v1";
#else
    constexpr const char* mappingName = "/MentalsMixRegistry_v1";
#endif

    // Monotonic milliseconds, used identically on every platform for the
    // slot-staleness timestamps (replaces the Windows-only GetTickCount64
    // this used to call).
    int64_t nowMs() noexcept
    {
        using namespace std::chrono;
        return (int64_t) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
    }
}

MixRegistry::MixRegistry()
{
#if defined(_WIN32)
    mappingHandle = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                         0, (DWORD) sizeof (SharedMemory), mappingName);
    if (mappingHandle == nullptr)
        return;

    // Whether this call created the mapping or opened an existing one made
    // by another process, the pages are zero-filled the first time any
    // process maps them -- exactly the "all slots free" state this type
    // needs, no explicit initialisation step required.
    memory = static_cast<SharedMemory*> (MapViewOfFile (mappingHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (SharedMemory)));
    if (memory == nullptr)
    {
        CloseHandle (mappingHandle);
        mappingHandle = nullptr;
        return;
    }
#else
    // POSIX equivalent of the Windows named-page-file-mapping trick above:
    // shm_open gives every process on the machine that asks for this same
    // name the same underlying memory object; O_CREAT is harmless if
    // another instance already created it. ftruncate actually sizes (and,
    // the first time, zero-fills) the object -- a process opening an
    // already-correctly-sized object is a cheap no-op. Unlike Windows,
    // nothing here automatically frees the segment when the last process
    // using it exits (see the destructor for why that's left alone
    // deliberately), but this class's own stale-slot-aging logic already
    // makes any leftover state from a previous run harmless either way.
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

    // Unique across processes and across instances within one process:
    // process ID in the high bits, a per-instance counter plus this
    // object's address in the low bits.
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

MixRegistry::~MixRegistry()
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
    // Deliberately not shm_unlink()'d -- POSIX shared memory has no
    // Windows-style "freed once the last handle closes" refcounting, so
    // unlinking here would yank the segment out from under every other
    // still-running instance. It's a small, fixed, well-known name that
    // simply persists (like a lock file would); a fresh process opening it
    // after everyone else has exited just sees stale slots, which are
    // already ignored by computeContext()'s own staleness check.
#endif
}

bool MixRegistry::claimSlot()
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

void MixRegistry::publish (const std::array<float, numOwnFeatures>& ownFeatures, float rotationDeg, float widthPercent) noexcept
{
    if (memory == nullptr || ownSlotIndex < 0)
        return;

    auto& slot = memory->slots[ownSlotIndex];
    for (int i = 0; i < numOwnFeatures; ++i)
        slot.features[i].store (ownFeatures[(size_t) i], std::memory_order_relaxed);
    slot.rotationDeg.store (rotationDeg, std::memory_order_relaxed);
    slot.widthPercent.store (widthPercent, std::memory_order_relaxed);
    slot.lastUpdateMs.store (nowMs(), std::memory_order_release);
}

MixRegistry::AggregateContext MixRegistry::computeContext (int64_t stalenessMs) const
{
    AggregateContext ctx;
    if (memory == nullptr)
        return ctx;

    const int64_t now = nowMs();

    for (int i = 0; i < maxSlots; ++i)
    {
        if (i == ownSlotIndex)
            continue;

        const auto& slot = memory->slots[i];
        if (slot.ownerId.load (std::memory_order_acquire) == 0)
            continue;
        if (now - slot.lastUpdateMs.load (std::memory_order_acquire) > stalenessMs)
            continue; // stale -- likely an instance that was closed/crashed without releasing its slot

        const float lowRatio  = slot.features[featureLowRatio].load (std::memory_order_relaxed);
        const float midRatio  = slot.features[featureMidRatio].load (std::memory_order_relaxed);
        const float highRatio = slot.features[featureHighRatio].load (std::memory_order_relaxed);
        const float rmsDb     = slot.features[featureRmsDb].load (std::memory_order_relaxed);
        const float rotation  = slot.rotationDeg.load (std::memory_order_relaxed);

        // Louder tracks occupy their region more meaningfully than quiet
        // ones -- weight occupancy by linear loudness, capped so one very
        // hot track can't make the histogram degenerate.
        const float weight = std::min (4.0f, std::pow (10.0f, rmsDb / 20.0f));

        if (rotation > 25.0f)
            ctx.rightOccupancy += weight;
        else if (rotation < -25.0f)
            ctx.leftOccupancy += weight;
        else
            ctx.centreOccupancy += weight;

        ctx.othersAvgLowRatio  += lowRatio;
        ctx.othersAvgMidRatio  += midRatio;
        ctx.othersAvgHighRatio += highRatio;
        ctx.othersCount++;
    }

    if (ctx.othersCount > 0)
    {
        ctx.othersAvgLowRatio  /= (float) ctx.othersCount;
        ctx.othersAvgMidRatio  /= (float) ctx.othersCount;
        ctx.othersAvgHighRatio /= (float) ctx.othersCount;
    }

    return ctx;
}
