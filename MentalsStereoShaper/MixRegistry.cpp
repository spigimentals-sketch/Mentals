#include "MixRegistry.h"
#include <processthreadsapi.h>
#include <cmath>
#include <algorithm>

namespace
{
    constexpr wchar_t mappingName[] = L"Local\\MentalsMixRegistry_v1";
}

MixRegistry::MixRegistry()
{
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

    // Unique across processes and across instances within one process:
    // process ID in the high bits, a per-instance counter plus this
    // object's address in the low bits.
    static std::atomic<uint32_t> instanceCounter { 0 };
    const uint32_t low = instanceCounter.fetch_add (1) ^ (uint32_t) reinterpret_cast<uintptr_t> (this);
    ownerId = ((uint64_t) GetCurrentProcessId() << 32) | (uint64_t) low;
    if (ownerId == 0)
        ownerId = 1;

    claimSlot();
}

MixRegistry::~MixRegistry()
{
    if (memory != nullptr && ownSlotIndex >= 0)
        memory->slots[ownSlotIndex].ownerId.store (0, std::memory_order_release);

    if (memory != nullptr)
        UnmapViewOfFile (memory);
    if (mappingHandle != nullptr)
        CloseHandle (mappingHandle);
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
    slot.lastUpdateMs.store ((int64_t) GetTickCount64(), std::memory_order_release);
}

MixRegistry::AggregateContext MixRegistry::computeContext (int64_t stalenessMs) const
{
    AggregateContext ctx;
    if (memory == nullptr)
        return ctx;

    const int64_t now = (int64_t) GetTickCount64();

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
