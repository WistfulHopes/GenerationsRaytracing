#pragma once

#include "FreeListAllocator.h"
#include "InstanceMask.h"
#include "VertexBuffer.h"

class InstanceInfoEx : public Hedgehog::Mirage::CInstanceInfo
{
public:
    uint32_t m_instanceIds[_countof(s_instanceMasks)];
    uint32_t m_bottomLevelAccelStructIds[_countof(s_instanceMasks)];
    ComPtr<VertexBuffer> m_poseVertexBuffer;
    uint32_t m_headNodeIndex;
    bool m_handledEyeMaterials;
    XXH32_hash_t m_modelHash;
    uint32_t m_visibilityBits;
    uint32_t m_hashFrame;
};

struct InstanceData
{
    static inline FreeListAllocator s_idAllocator;

    static void createPendingInstances();

    static void trackInstance(InstanceInfoEx* instanceInfoEx);
    static void releaseUnusedInstances();

    static void init();
};