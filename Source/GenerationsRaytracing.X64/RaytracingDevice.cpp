#include "RaytracingDevice.h"

#include "DXILLibrary.h"
#include "CopyTextureVertexShader.h"
#include "CopyTexturePixelShader.h"
#include "DLSS.h"
#include "EnvironmentColor.h"
#include "GeometryFlags.h"
#include "Message.h"
#include "RootSignature.h"
#include "PoseComputeShader.h"
#include "ResolveComputeShader.h"

static constexpr uint32_t SCRATCH_BUFFER_SIZE = 16 * 1024 * 1024;

uint32_t RaytracingDevice::allocateGeometryDescs(uint32_t count)
{
    const auto it = m_freeCounts.lower_bound(count);
    if (it != m_freeCounts.end())
    {
        const uint32_t remnant = it->first - count;
        const uint32_t index = it->second + remnant;

        const auto it2 = m_freeIndices.find(it->second);
        assert(it2 != m_freeIndices.end());

        if (remnant > 0)
        {
            auto node = m_freeCounts.extract(it);
            node.key() = remnant;
            it2->second = m_freeCounts.insert(std::move(node));
        }
        else
        {
            m_freeIndices.erase(it2);
            m_freeCounts.erase(it);
        }

        return index;
    }
    else
    {
        const uint32_t index = static_cast<uint32_t>(m_geometryDescs.size());
        m_geometryDescs.resize(m_geometryDescs.size() + count);
        return index;
    }
}

void RaytracingDevice::freeGeometryDescs(uint32_t id, uint32_t count)
{
    const auto next = m_freeIndices.lower_bound(id);
    if (next != m_freeIndices.end())
    {
        if (next != m_freeIndices.begin())
        {
            const auto prev = std::prev(next);

            // Merge previous range (prevId + prevCount) == id
            if ((prev->first + prev->second->first) == id)
            {
                id = prev->first;
                count += prev->second->first;

                m_freeCounts.erase(prev->second);
                m_freeIndices.erase(prev->first);
            }
        }
        // Merge next range (id + count) == nextId
        if ((id + count) == next->first)
        {
            count += next->second->first;
            m_freeCounts.erase(next->second);
            m_freeIndices.erase(next->first);
        }
    }
    m_freeIndices.emplace(id, m_freeCounts.emplace(count, id));
}

uint32_t RaytracingDevice::buildRaytracingAccelerationStructure(ComPtr<D3D12MA::Allocation>& allocation,
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& accelStructDesc, bool buildImmediate)
{
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuildInfo{};
    m_device->GetRaytracingAccelerationStructurePrebuildInfo(&accelStructDesc.Inputs, &preBuildInfo);

    if (allocation == nullptr || allocation->GetSize() < preBuildInfo.ResultDataMaxSizeInBytes)
    {
        if (allocation != nullptr)
            m_tempBuffers[m_frame].push_back(std::move(allocation));

        createBuffer(
            D3D12_HEAP_TYPE_DEFAULT,
            static_cast<uint32_t>(preBuildInfo.ResultDataMaxSizeInBytes),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            allocation);
    }

    accelStructDesc.DestAccelerationStructureData = allocation->GetResource()->GetGPUVirtualAddress();

    if (buildImmediate)
    {
        // Create separate resource if it does not fit to the pool
        if (m_scratchBufferOffset + preBuildInfo.ScratchDataSizeInBytes > SCRATCH_BUFFER_SIZE)
        {
            auto& scratchBuffer = m_tempBuffers[m_frame].emplace_back();

            createBuffer(
                D3D12_HEAP_TYPE_DEFAULT,
                static_cast<uint32_t>(preBuildInfo.ScratchDataSizeInBytes),
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COMMON,
                scratchBuffer);

            accelStructDesc.ScratchAccelerationStructureData = scratchBuffer->GetResource()->GetGPUVirtualAddress();
        }
        else
        {
            accelStructDesc.ScratchAccelerationStructureData = m_scratchBuffers[m_frame]->GetResource()->GetGPUVirtualAddress() + m_scratchBufferOffset;

            m_scratchBufferOffset = static_cast<uint32_t>((m_scratchBufferOffset + preBuildInfo.ScratchDataSizeInBytes
                + D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1) & ~(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1));
        }

        getUnderlyingGraphicsCommandList()->BuildRaytracingAccelerationStructure(&accelStructDesc, 0, nullptr);
        getGraphicsCommandList().uavBarrier(allocation->GetResource());
    }

    return static_cast<uint32_t>(preBuildInfo.ScratchDataSizeInBytes);
}

void RaytracingDevice::handlePendingBottomLevelAccelStructBuilds()
{
    for (const auto pendingPose : m_pendingPoses)
        getGraphicsCommandList().uavBarrier(m_vertexBuffers[pendingPose].allocation->GetResource());

    getGraphicsCommandList().commitBarriers();

    for (const auto pendingBuild : m_pendingBuilds)
    {
        auto& bottomLevelAccelStruct = m_bottomLevelAccelStructs[pendingBuild];

        // Create separate resource if it does not fit to the pool
        if (m_scratchBufferOffset + bottomLevelAccelStruct.scratchBufferSize > SCRATCH_BUFFER_SIZE)
        {
            auto& scratchBuffer = m_tempBuffers[m_frame].emplace_back();

            createBuffer(
                D3D12_HEAP_TYPE_DEFAULT,
                bottomLevelAccelStruct.scratchBufferSize,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COMMON,
                scratchBuffer);

            bottomLevelAccelStruct.desc.ScratchAccelerationStructureData = scratchBuffer->GetResource()->GetGPUVirtualAddress();
        }
        else
        {
            bottomLevelAccelStruct.desc.ScratchAccelerationStructureData =
                m_scratchBuffers[m_frame]->GetResource()->GetGPUVirtualAddress() + m_scratchBufferOffset;

            m_scratchBufferOffset = m_scratchBufferOffset + bottomLevelAccelStruct.scratchBufferSize
                + D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1 & ~(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1);
        }

        getUnderlyingGraphicsCommandList()->BuildRaytracingAccelerationStructure(&bottomLevelAccelStruct.desc, 0, nullptr);
        getGraphicsCommandList().uavBarrier(bottomLevelAccelStruct.allocation->GetResource());
    }

    m_pendingPoses.clear();
    m_pendingBuilds.clear();
}

static float haltonSequence(int i, int b)
{
    float f = 1.0;
    float r = 0.0;

    while (i > 0)
    {
        f = f / float(b);
        r = r + f * float(i % b);
        i = i / b;
    }

    return r;
}

static void haltonJitter(int frame, int phases, float& jitterX, float& jitterY)
{
    jitterX = haltonSequence(frame % phases + 1, 2) - 0.5f;
    jitterY = haltonSequence(frame % phases + 1, 3) - 0.5f;
}

D3D12_GPU_VIRTUAL_ADDRESS RaytracingDevice::createGlobalsRT()
{
    EnvironmentColor::get(m_globalsPS, m_globalsRT.environmentColor);

    haltonJitter(m_currentFrame, 64, m_globalsRT.pixelJitterX, m_globalsRT.pixelJitterY);
    ++m_currentFrame;

    static std::default_random_engine engine;
    static std::uniform_int_distribution distribution(0, 1024);

    m_globalsRT.blueNoiseOffsetX = distribution(engine);
    m_globalsRT.blueNoiseOffsetY = distribution(engine);

    const auto globalsRT = createBuffer(&m_globalsRT, sizeof(GlobalsRT), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    memcpy(m_globalsRT.prevProj, m_globalsVS.floatConstants, 0x80);

    return globalsRT;
}

void RaytracingDevice::createTopLevelAccelStruct()
{
    handlePendingBottomLevelAccelStructBuilds();

    for (const auto& [textureId, textureIdOffset] : m_delayedTextures)
    {
        if (m_textures.size() > textureId &&
            m_textures[textureId].allocation != nullptr)
        {
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(m_materials.data()) + textureIdOffset) |= m_textures[textureId].srvIndex;
        }
        else
        {
            // Still not loaded, leave it for next creation
            m_tempDelayedTextures.emplace_back(textureId, textureIdOffset);
        }
    }

    for (const auto& [instanceId, bottomLevelAccelStructId] : m_delayedInstances)
    {
        if (m_bottomLevelAccelStructs.size() > bottomLevelAccelStructId &&
            m_bottomLevelAccelStructs[bottomLevelAccelStructId].allocation != nullptr)
        {
            auto& instanceDesc = m_instanceDescs[instanceId];
            auto& bottomLevelAccelStruct = m_bottomLevelAccelStructs[bottomLevelAccelStructId];
            instanceDesc.InstanceID = bottomLevelAccelStruct.geometryId;
            instanceDesc.InstanceMask = 1;
            instanceDesc.AccelerationStructure = bottomLevelAccelStruct.allocation->GetResource()->GetGPUVirtualAddress();
        }
        else
        {
            // Still not loaded, leave it for next creation
            m_tempDelayedInstances.emplace_back(instanceId, bottomLevelAccelStructId);
        }
    }

    m_delayedTextures.clear();
    m_delayedInstances.clear();

    std::swap(m_delayedTextures, m_tempDelayedTextures);
    std::swap(m_delayedInstances, m_tempDelayedInstances);

    if (m_instanceDescs.empty())
        return;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC accelStructDesc{};
    accelStructDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    accelStructDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    accelStructDesc.Inputs.NumDescs = static_cast<UINT>(m_instanceDescs.size());
    accelStructDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    accelStructDesc.Inputs.InstanceDescs = createBuffer(m_instanceDescs.data(),
        static_cast<uint32_t>(m_instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC)), D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT);

    getGraphicsCommandList().commitBarriers();
    buildRaytracingAccelerationStructure(m_topLevelAccelStruct, accelStructDesc, true);
}

void RaytracingDevice::createRaytracingTextures()
{
    m_upscaler->init({ *this, m_width, m_height, m_qualityMode });

    D3D12MA::ALLOCATION_DESC allocDesc{};
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
    allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;

    auto resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_UNKNOWN,
        m_upscaler->getWidth(), m_upscaler->getHeight(), 1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    struct
    {
        DXGI_FORMAT format;
        ComPtr<D3D12MA::Allocation>& allocation;
    }
    const textureDescs[] =
    {
        { DXGI_FORMAT_R16G16B16A16_FLOAT, m_colorTexture },
        { DXGI_FORMAT_R32_FLOAT, m_depthTexture },
        { DXGI_FORMAT_R16G16_FLOAT, m_motionVectorsTexture },
        { DXGI_FORMAT_R32G32B32A32_FLOAT, m_positionAndFlagsTexture },
        { DXGI_FORMAT_R10G10B10A2_UNORM, m_normalTexture },
        { DXGI_FORMAT_R16G16B16A16_FLOAT, m_diffuseTexture },
        { DXGI_FORMAT_R16G16B16A16_FLOAT, m_specularTexture },
        { DXGI_FORMAT_R32_FLOAT, m_specularPowerTexture },
        { DXGI_FORMAT_R32_FLOAT, m_specularLevelTexture },
        { DXGI_FORMAT_R16G16B16A16_FLOAT, m_emissionTexture },
        { DXGI_FORMAT_R16G16B16A16_FLOAT, m_falloffTexture },
        { DXGI_FORMAT_R16_UNORM, m_shadowTexture },
        { DXGI_FORMAT_R32G32B32A32_FLOAT, m_globalIlluminationTexture },
        { DXGI_FORMAT_R32G32B32A32_FLOAT, m_reflectionTexture },
    };

    if (m_uavId == NULL)
        m_uavId = m_descriptorHeap.allocateMany(_countof(textureDescs));

    auto cpuHandle = m_descriptorHeap.getCpuHandle(m_uavId);

    for (const auto& textureDesc : textureDescs)
    {
        resourceDesc.Format = textureDesc.format;

        const HRESULT hr = m_allocator->CreateResource(
            &allocDesc,
            &resourceDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            textureDesc.allocation.ReleaseAndGetAddressOf(),
            IID_ID3D12Resource,
            nullptr);

        assert(SUCCEEDED(hr) && textureDesc.allocation != nullptr);

        m_device->CreateUnorderedAccessView(
            textureDesc.allocation->GetResource(),
            nullptr,
            nullptr,
            cpuHandle);

        cpuHandle.ptr += m_descriptorHeap.getIncrementSize();
    }

    resourceDesc.Width = m_width;
    resourceDesc.Height = m_height;
    resourceDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

    const HRESULT hr = m_allocator->CreateResource(
        &allocDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        m_outputTexture.ReleaseAndGetAddressOf(),
        IID_ID3D12Resource,
        nullptr);

    assert(SUCCEEDED(hr) && m_outputTexture != nullptr);

    // Create descriptor heap for copy texture shader
    if (m_srvId == NULL)
        m_srvId = m_descriptorHeap.allocateMany(2);

    cpuHandle = m_descriptorHeap.getCpuHandle(m_srvId);
    m_device->CreateShaderResourceView(m_outputTexture->GetResource(), nullptr, cpuHandle);

    cpuHandle.ptr += m_descriptorHeap.getIncrementSize();
    m_device->CreateShaderResourceView(m_depthTexture->GetResource(), nullptr, cpuHandle);
}

void RaytracingDevice::resolveAndDispatchUpscaler()
{
    getUnderlyingGraphicsCommandList()->SetPipelineState(m_resolvePipeline.Get());

    getGraphicsCommandList().uavBarrier(m_diffuseTexture->GetResource());
    getGraphicsCommandList().uavBarrier(m_specularTexture->GetResource());
    getGraphicsCommandList().uavBarrier(m_specularLevelTexture->GetResource());
    getGraphicsCommandList().uavBarrier(m_emissionTexture->GetResource());
    getGraphicsCommandList().uavBarrier(m_falloffTexture->GetResource());
    getGraphicsCommandList().uavBarrier(m_shadowTexture->GetResource());
    getGraphicsCommandList().uavBarrier(m_globalIlluminationTexture->GetResource());
    getGraphicsCommandList().uavBarrier(m_reflectionTexture->GetResource());

    getGraphicsCommandList().commitBarriers();

    getUnderlyingGraphicsCommandList()->Dispatch(
        (m_upscaler->getWidth() + 31) / 32,
        (m_upscaler->getHeight() + 31) / 32,
        1);

    getGraphicsCommandList().uavBarrier(m_colorTexture->GetResource());
    getGraphicsCommandList().uavBarrier(m_depthTexture->GetResource());

    getGraphicsCommandList().transitionBarrier(m_colorTexture->GetResource(), 
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    getGraphicsCommandList().transitionBarrier(m_depthTexture->GetResource(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    getGraphicsCommandList().commitBarriers();

    m_upscaler->dispatch(
        {
            *this,
            m_colorTexture->GetResource(),
            m_outputTexture->GetResource(),
            m_depthTexture->GetResource(),
            m_motionVectorsTexture->GetResource(),
            m_globalsRT.pixelJitterX,
            m_globalsRT.pixelJitterY,
            false
        });

    ID3D12DescriptorHeap* descriptorHeaps[] =
    {
        m_descriptorHeap.getUnderlyingHeap(),
        m_samplerDescriptorHeap.getUnderlyingHeap()
    };

    getUnderlyingGraphicsCommandList()->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
}

void RaytracingDevice::copyToRenderTargetAndDepthStencil()
{
    const D3D12_VIEWPORT viewport = { 0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f };
    const D3D12_RECT scissorRect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };

    getUnderlyingGraphicsCommandList()->SetGraphicsRootSignature(m_copyTextureRootSignature.Get());
    getUnderlyingGraphicsCommandList()->SetGraphicsRootDescriptorTable(0, m_descriptorHeap.getGpuHandle(m_srvId));
    getUnderlyingGraphicsCommandList()->OMSetRenderTargets(1, &m_renderTargetView, FALSE, &m_depthStencilView);
    getUnderlyingGraphicsCommandList()->SetPipelineState(m_copyTexturePipeline.Get());
    getUnderlyingGraphicsCommandList()->RSSetViewports(1, &viewport);
    getUnderlyingGraphicsCommandList()->RSSetScissorRects(1, &scissorRect);
    getUnderlyingGraphicsCommandList()->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    getGraphicsCommandList().transitionBarrier(m_outputTexture->GetResource(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    getGraphicsCommandList().transitionBarrier(m_depthTexture->GetResource(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    getGraphicsCommandList().commitBarriers();

    getUnderlyingGraphicsCommandList()->DrawInstanced(6, 1, 0, 0);

    m_dirtyFlags |=
        DIRTY_FLAG_ROOT_SIGNATURE |
        DIRTY_FLAG_PIPELINE_DESC |
        DIRTY_FLAG_GLOBALS_PS |
        DIRTY_FLAG_GLOBALS_VS |
        DIRTY_FLAG_VIEWPORT |
        DIRTY_FLAG_SCISSOR_RECT |
        DIRTY_FLAG_PRIMITIVE_TOPOLOGY;

    m_dirtyFlags &= ~DIRTY_FLAG_RENDER_TARGET_AND_DEPTH_STENCIL;
}

void RaytracingDevice::procMsgCreateBottomLevelAccelStruct()
{
    const auto& message = m_messageReceiver.getMessage<MsgCreateBottomLevelAccelStruct>();

    if (m_bottomLevelAccelStructs.size() <= message.bottomLevelAccelStructId)
        m_bottomLevelAccelStructs.resize(message.bottomLevelAccelStructId + 1);

    auto& bottomLevelAccelStruct = m_bottomLevelAccelStructs[message.bottomLevelAccelStructId];

    bottomLevelAccelStruct.geometryDescs.resize(message.dataSize / sizeof(MsgCreateBottomLevelAccelStruct::GeometryDesc));
    bottomLevelAccelStruct.geometryCount = static_cast<uint32_t>(bottomLevelAccelStruct.geometryDescs.size());
    bottomLevelAccelStruct.geometryId = allocateGeometryDescs(bottomLevelAccelStruct.geometryCount);

    for (size_t i = 0; i < bottomLevelAccelStruct.geometryDescs.size(); i++)
    {
        const auto& geometryDesc =
            reinterpret_cast<const MsgCreateBottomLevelAccelStruct::GeometryDesc*>(message.data)[i];

        assert((geometryDesc.indexCount % 3) == 0);
        assert(m_indexBuffers[geometryDesc.indexBufferId].byteSize >= geometryDesc.indexCount * sizeof(uint16_t));
        assert(m_vertexBuffers[geometryDesc.vertexBufferId].byteSize - geometryDesc.positionOffset >= geometryDesc.vertexCount * geometryDesc.vertexStride);
        assert((geometryDesc.positionOffset % 4) == 0);

        // BVH GeometryDesc
        {
            auto& dstGeometryDesc = bottomLevelAccelStruct.geometryDescs[i];

            dstGeometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;

            dstGeometryDesc.Flags = (geometryDesc.flags & (GEOMETRY_FLAG_TRANSPARENT | GEOMETRY_FLAG_PUNCH_THROUGH)) ? 
                D3D12_RAYTRACING_GEOMETRY_FLAG_NONE : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

            auto& triangles = dstGeometryDesc.Triangles;

            triangles.Transform3x4 = NULL;
            triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
            triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            triangles.IndexCount = geometryDesc.indexCount;
            triangles.VertexCount = geometryDesc.vertexCount;
            triangles.IndexBuffer = m_indexBuffers[geometryDesc.indexBufferId].allocation->GetResource()->GetGPUVirtualAddress();
            triangles.VertexBuffer.StartAddress = m_vertexBuffers[geometryDesc.vertexBufferId].allocation->GetResource()->GetGPUVirtualAddress() + geometryDesc.positionOffset;
            triangles.VertexBuffer.StrideInBytes = geometryDesc.vertexStride;
        }

        // GPU GeometryDesc
        {
            auto& dstGeometryDesc = m_geometryDescs[bottomLevelAccelStruct.geometryId + i];

            dstGeometryDesc.flags = geometryDesc.flags;
            dstGeometryDesc.indexBufferId = m_indexBuffers[geometryDesc.indexBufferId].srvIndex;
            dstGeometryDesc.vertexBufferId = m_vertexBuffers[geometryDesc.vertexBufferId].srvIndex;
            dstGeometryDesc.vertexStride = geometryDesc.vertexStride;
            dstGeometryDesc.positionOffset = geometryDesc.positionOffset;
            dstGeometryDesc.normalOffset = geometryDesc.normalOffset;
            dstGeometryDesc.tangentOffset = geometryDesc.tangentOffset;
            dstGeometryDesc.binormalOffset = geometryDesc.binormalOffset;
            memcpy(dstGeometryDesc.texCoordOffsets, geometryDesc.texCoordOffsets, sizeof(dstGeometryDesc.texCoordOffsets));
            dstGeometryDesc.colorOffset = geometryDesc.colorOffset;
            dstGeometryDesc.materialId = geometryDesc.materialId;
        }
    }

    bottomLevelAccelStruct.desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottomLevelAccelStruct.desc.Inputs.Flags = message.preferFastBuild ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    bottomLevelAccelStruct.desc.Inputs.NumDescs = static_cast<UINT>(bottomLevelAccelStruct.geometryDescs.size());
    bottomLevelAccelStruct.desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    bottomLevelAccelStruct.desc.Inputs.pGeometryDescs = bottomLevelAccelStruct.geometryDescs.data();
    bottomLevelAccelStruct.scratchBufferSize = buildRaytracingAccelerationStructure(bottomLevelAccelStruct.allocation, bottomLevelAccelStruct.desc, false);
    m_pendingBuilds.push_back(message.bottomLevelAccelStructId);
}

void RaytracingDevice::procMsgReleaseRaytracingResource()
{
    const auto& message = m_messageReceiver.getMessage<MsgReleaseRaytracingResource>();

    switch (message.resourceType)
    {
    case MsgReleaseRaytracingResource::ResourceType::BottomLevelAccelStruct:
    {
        auto& bottomLevelAccelStruct = m_bottomLevelAccelStructs[message.resourceId];
        m_tempBuffers[m_frame].emplace_back(std::move(bottomLevelAccelStruct.allocation));
        freeGeometryDescs(m_bottomLevelAccelStructs[message.resourceId].geometryId, m_bottomLevelAccelStructs[message.resourceId].geometryCount);
        bottomLevelAccelStruct.desc = {};
        bottomLevelAccelStruct.geometryDescs.clear();
        break;
    }

    case MsgReleaseRaytracingResource::ResourceType::Instance:
        memset(&m_instanceDescs[message.resourceId], 0, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));

        if (m_instanceGeometries.size() > message.resourceId)
        {
            auto& instanceGeometries = m_instanceGeometries[message.resourceId];
            if (instanceGeometries.first != NULL)
                freeGeometryDescs(instanceGeometries.first, instanceGeometries.second);

            instanceGeometries = {};
        }

        break;

    case MsgReleaseRaytracingResource::ResourceType::Material:
        memset(&m_materials[message.resourceId], 0, sizeof(Material));
        break;

    default:
        assert(false);
        break;
    }
}

void RaytracingDevice::procMsgCreateInstance()
{
    const auto& message = m_messageReceiver.getMessage<MsgCreateInstance>();

    if (m_instanceDescs.size() <= message.instanceId)
        m_instanceDescs.resize(message.instanceId + 1);

    auto& instanceDesc = m_instanceDescs[message.instanceId];

    memcpy(instanceDesc.Transform, message.transform, sizeof(message.transform));

    if (m_bottomLevelAccelStructs.size() <= message.bottomLevelAccelStructId ||
        m_bottomLevelAccelStructs[message.bottomLevelAccelStructId].allocation == nullptr)
    {
        // Not loaded yet, defer assignment to top level acceleration structure creation
        m_delayedInstances.emplace_back(message.instanceId, message.bottomLevelAccelStructId);
        assert(message.dataSize == 0); // Skinned models should not fall here!
    }
    else
    {
        auto& bottomLevelAccelStruct = m_bottomLevelAccelStructs[message.bottomLevelAccelStructId];

        if (message.dataSize > 0)
        {
            if (m_instanceGeometries.size() <= message.instanceId)
                m_instanceGeometries.resize(message.instanceId + 1);

            auto& [geometryId, geometryCount] = m_instanceGeometries[message.instanceId];

            if (geometryId == NULL)
            {
                geometryId = allocateGeometryDescs(bottomLevelAccelStruct.geometryCount);
                geometryCount = bottomLevelAccelStruct.geometryCount;

                memcpy(&m_geometryDescs[geometryId], &m_geometryDescs[bottomLevelAccelStruct.geometryId],
                    bottomLevelAccelStruct.geometryCount * sizeof(GeometryDesc));

                instanceDesc.InstanceID = geometryId;
            }

            for (size_t i = 0; i < geometryCount; i++)
            {
                const auto& srcGeometry = m_geometryDescs[bottomLevelAccelStruct.geometryId + i];
                auto& dstGeometry = m_geometryDescs[geometryId + i];

                dstGeometry.materialId = srcGeometry.materialId;

                auto curId = reinterpret_cast<const uint32_t*>(message.data);
                const auto lastId = reinterpret_cast<const uint32_t*>(message.data + message.dataSize);

                while (curId < lastId)
                {
                    if ((*curId) == srcGeometry.materialId)
                    {
                        dstGeometry.materialId = *(curId + 1);
                        break;
                    }
                    else
                    {
                        curId += 2;
                    }
                }
            }
        }
        else
        {
            instanceDesc.InstanceID = bottomLevelAccelStruct.geometryId;
        }

        instanceDesc.InstanceMask = 1;
        instanceDesc.AccelerationStructure = bottomLevelAccelStruct.allocation->GetResource()->GetGPUVirtualAddress();
    }
}

void RaytracingDevice::procMsgTraceRays()
{
    const auto& message = m_messageReceiver.getMessage<MsgTraceRays>();

    m_globalsRT.blueNoiseTextureId = m_textures[message.blueNoiseTextureId].srvIndex;

    if (m_width != message.width || m_height != message.height)
    {
        m_width = message.width;
        m_height = message.height;
        createRaytracingTextures();
    }

    createTopLevelAccelStruct();

    const auto globalsVS = createBuffer(&m_globalsVS,
        sizeof(m_globalsVS), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    const auto globalsPS = createBuffer(&m_globalsPS, 
        offsetof(GlobalsPS, textureIndices), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    const auto globalsRT = createGlobalsRT();

    const auto topLevelAccelStruct = m_topLevelAccelStruct != nullptr ? 
        m_topLevelAccelStruct->GetResource()->GetGPUVirtualAddress() : NULL;

    const auto geometryDescs = createBuffer(m_geometryDescs.data(), 
        static_cast<uint32_t>(m_geometryDescs.size() * sizeof(GeometryDesc)), 0x4);

    const auto materials = createBuffer(m_materials.data(),
        static_cast<uint32_t>(m_materials.size() * sizeof(Material)), 0x4);

    if (m_curRootSignature != m_raytracingRootSignature.Get())
    {
        getUnderlyingGraphicsCommandList()->SetComputeRootSignature(m_raytracingRootSignature.Get());
        m_curRootSignature = m_raytracingRootSignature.Get();
    }
    getUnderlyingGraphicsCommandList()->SetPipelineState1(m_stateObject.Get());

    getUnderlyingGraphicsCommandList()->SetComputeRootConstantBufferView(0, globalsVS);
    getUnderlyingGraphicsCommandList()->SetComputeRootConstantBufferView(1, globalsPS);
    getUnderlyingGraphicsCommandList()->SetComputeRootConstantBufferView(2, globalsRT);
    getUnderlyingGraphicsCommandList()->SetComputeRootShaderResourceView(3, topLevelAccelStruct);
    getUnderlyingGraphicsCommandList()->SetComputeRootDescriptorTable(4, m_descriptorHeap.getGpuHandle(m_uavId));
    getUnderlyingGraphicsCommandList()->SetComputeRootShaderResourceView(5, geometryDescs);
    getUnderlyingGraphicsCommandList()->SetComputeRootShaderResourceView(6, materials);

    const auto shaderTable = createBuffer(
        m_shaderTable.data(), static_cast<uint32_t>(m_shaderTable.size()), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

    D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc{};

    dispatchRaysDesc.RayGenerationShaderRecord.StartAddress = shaderTable;
    dispatchRaysDesc.RayGenerationShaderRecord.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    dispatchRaysDesc.MissShaderTable.StartAddress = shaderTable + 8 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    dispatchRaysDesc.MissShaderTable.SizeInBytes = 2 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    dispatchRaysDesc.MissShaderTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    dispatchRaysDesc.HitGroupTable.StartAddress = shaderTable + 10 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    dispatchRaysDesc.HitGroupTable.SizeInBytes = 2 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    dispatchRaysDesc.HitGroupTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    dispatchRaysDesc.Width = m_upscaler->getWidth();
    dispatchRaysDesc.Height = m_upscaler->getHeight();
    dispatchRaysDesc.Depth = 1;

    // PrimaryRayGeneration
    getGraphicsCommandList().commitBarriers();
    getUnderlyingGraphicsCommandList()->DispatchRays(&dispatchRaysDesc);

    getGraphicsCommandList().uavBarrier(m_positionAndFlagsTexture->GetResource());
    getGraphicsCommandList().uavBarrier(m_normalTexture->GetResource());
    getGraphicsCommandList().uavBarrier(m_specularPowerTexture->GetResource());
    getGraphicsCommandList().commitBarriers();

    // ShadowRayGeneration
    dispatchRaysDesc.RayGenerationShaderRecord.StartAddress += 2 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    getUnderlyingGraphicsCommandList()->DispatchRays(&dispatchRaysDesc);

    // GlobalIlluminationRayGeneration
    dispatchRaysDesc.RayGenerationShaderRecord.StartAddress += 2 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    getUnderlyingGraphicsCommandList()->DispatchRays(&dispatchRaysDesc);

    // ReflectionRayGeneration
    dispatchRaysDesc.RayGenerationShaderRecord.StartAddress += 2 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    getUnderlyingGraphicsCommandList()->DispatchRays(&dispatchRaysDesc);

    resolveAndDispatchUpscaler();
    copyToRenderTargetAndDepthStencil();
}

void RaytracingDevice::procMsgCreateMaterial()
{
    const auto& message = m_messageReceiver.getMessage<MsgCreateMaterial>();

    if (m_materials.size() <= message.materialId)
        m_materials.resize(message.materialId + 1);

    auto& material = m_materials[message.materialId];

    material.shaderType = message.shaderType;

    const std::pair<const MsgCreateMaterial::Texture&, uint32_t&> textures[] =
    {
        { message.diffuseTexture,      material.diffuseTexture      },
        { message.diffuseTexture2,     material.diffuseTexture2     },
        { message.specularTexture,     material.specularTexture     },
        { message.specularTexture2,    material.specularTexture2    },
        { message.glossTexture,        material.glossTexture        },
        { message.glossTexture2,       material.glossTexture2       },
        { message.normalTexture,       material.normalTexture       },
        { message.normalTexture2,      material.normalTexture2      },
        { message.reflectionTexture,   material.reflectionTexture   },
        { message.opacityTexture,      material.opacityTexture      },
        { message.displacementTexture, material.displacementTexture },
    };

    D3D12_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

    for (const auto& [srcTexture, dstTexture] : textures)
    {
        dstTexture = NULL;

        if (srcTexture.id != NULL)
        {
            if (m_textures.size() <= srcTexture.id || m_textures[srcTexture.id].allocation == nullptr)
            {
                // Delay texture assignment if the texture is not loaded yet
                m_delayedTextures.emplace_back(srcTexture.id,
                    reinterpret_cast<uintptr_t>(&dstTexture) - reinterpret_cast<uintptr_t>(m_materials.data()));
            }
            else
            {
                dstTexture = m_textures[srcTexture.id].srvIndex;
            }

            samplerDesc.AddressU = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(srcTexture.addressModeU);
            samplerDesc.AddressV = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(srcTexture.addressModeV);

            auto& sampler = m_samplers[XXH3_64bits(&samplerDesc, sizeof(samplerDesc))];
            if (!sampler)
            {
                sampler = m_samplerDescriptorHeap.allocate();

                m_device->CreateSampler(&samplerDesc,
                    m_samplerDescriptorHeap.getCpuHandle(sampler));
            }

            dstTexture |= sampler << 20;
            dstTexture |= srcTexture.texCoordIndex << 30;
        }
    }

    memcpy(material.texCoordOffsets, message.texCoordOffsets, sizeof(Material) - offsetof(Material, texCoordOffsets));
}

void RaytracingDevice::procMsgBuildBottomLevelAccelStruct()
{
    const auto& message = m_messageReceiver.getMessage<MsgBuildBottomLevelAccelStruct>();
    m_pendingBuilds.push_back(message.bottomLevelAccelStructId);
}

void RaytracingDevice::procMsgComputePose()
{
    const auto& message = m_messageReceiver.getMessage<MsgComputePose>();

    if (m_curRootSignature != m_poseRootSignature.Get())
    {
        getUnderlyingGraphicsCommandList()->SetComputeRootSignature(m_poseRootSignature.Get());
        m_curRootSignature = m_poseRootSignature.Get();
    }
    if (m_curPipeline != m_posePipeline.Get())
    {
        getUnderlyingGraphicsCommandList()->SetPipelineState(m_posePipeline.Get());
        m_curPipeline = m_posePipeline.Get();
        m_dirtyFlags |= DIRTY_FLAG_PIPELINE_DESC;
    }

    getUnderlyingGraphicsCommandList()->SetComputeRootConstantBufferView(0,
        createBuffer(message.data, message.nodeCount * sizeof(float[4][4]), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));

    auto geometryDesc = reinterpret_cast<const MsgComputePose::GeometryDesc*>(
        message.data + message.nodeCount * sizeof(float[4][4]));

    const auto destVertexBuffer = m_vertexBuffers[message.vertexBufferId].allocation->GetResource();
    auto destVirtualAddress = destVertexBuffer->GetGPUVirtualAddress();

    for (size_t i = 0; i < message.geometryCount; i++)
    {
        struct GeometryDesc
        {
            uint32_t vertexCount;
            uint32_t vertexStride;
            uint32_t normalOffset;
            uint32_t tangentOffset;
            uint32_t binormalOffset;
            uint32_t blendWeightOffset;
            uint32_t blendIndicesOffset;
            uint32_t padding0;
            uint32_t nodePalette[25];
        };

        GeometryDesc dstGeometryDesc;
        dstGeometryDesc.vertexCount = geometryDesc->vertexCount;
        dstGeometryDesc.vertexStride = geometryDesc->vertexStride;
        dstGeometryDesc.normalOffset = geometryDesc->normalOffset;
        dstGeometryDesc.tangentOffset = geometryDesc->tangentOffset;
        dstGeometryDesc.binormalOffset = geometryDesc->binormalOffset;
        dstGeometryDesc.blendWeightOffset = geometryDesc->blendWeightOffset;
        dstGeometryDesc.blendIndicesOffset = geometryDesc->blendIndicesOffset;

        for (size_t j = 0; j < 25; j++)
            dstGeometryDesc.nodePalette[j] = geometryDesc->nodePalette[j];

        getUnderlyingGraphicsCommandList()->SetComputeRootConstantBufferView(1,
            createBuffer(&dstGeometryDesc, sizeof(GeometryDesc), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));

        getUnderlyingGraphicsCommandList()->SetComputeRootShaderResourceView(2,
            m_vertexBuffers[geometryDesc->vertexBufferId].allocation->GetResource()->GetGPUVirtualAddress());

        getUnderlyingGraphicsCommandList()->SetComputeRootUnorderedAccessView(3, destVirtualAddress);

        getUnderlyingGraphicsCommandList()->Dispatch((geometryDesc->vertexCount + 63) / 64, 1, 1);

        destVirtualAddress += geometryDesc->vertexCount * geometryDesc->vertexStride;
        ++geometryDesc;
    }
    m_pendingPoses.push_back(message.vertexBufferId);
}

bool RaytracingDevice::processRaytracingMessage()
{
    switch (m_messageReceiver.getId())
    {
    case MsgCreateBottomLevelAccelStruct::s_id: procMsgCreateBottomLevelAccelStruct(); break;
    case MsgReleaseRaytracingResource::s_id: procMsgReleaseRaytracingResource(); break;
    case MsgCreateInstance::s_id: procMsgCreateInstance(); break;
    case MsgTraceRays::s_id: procMsgTraceRays(); break;
    case MsgCreateMaterial::s_id: procMsgCreateMaterial(); break;
    case MsgComputePose::s_id: procMsgComputePose(); break;
    case MsgBuildBottomLevelAccelStruct::s_id: procMsgBuildBottomLevelAccelStruct(); break;
    default: return false;
    }

    return true;
}

void RaytracingDevice::releaseRaytracingResources()
{
    m_curRootSignature = nullptr;
    m_curPipeline = nullptr;
    m_scratchBufferOffset = 0;
}

RaytracingDevice::RaytracingDevice()
{
    if (m_device == nullptr)
        return;

    CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[1];
    descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 14, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

    CD3DX12_ROOT_PARAMETER1 raytracingRootParams[7];
    raytracingRootParams[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    raytracingRootParams[1].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    raytracingRootParams[2].InitAsConstantBufferView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    raytracingRootParams[3].InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    raytracingRootParams[4].InitAsDescriptorTable(_countof(descriptorRanges), descriptorRanges);
    raytracingRootParams[5].InitAsShaderResourceView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    raytracingRootParams[6].InitAsShaderResourceView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);

    RootSignature::create(
        m_device.Get(),
        raytracingRootParams,
        _countof(raytracingRootParams),
        nullptr,
        0,
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | 
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED,
        m_raytracingRootSignature);

    CD3DX12_STATE_OBJECT_DESC stateObject(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

    CD3DX12_DXIL_LIBRARY_SUBOBJECT dxilLibrarySubobject(stateObject);
    const CD3DX12_SHADER_BYTECODE dxilLibrary(DXILLibrary, sizeof(DXILLibrary));
    dxilLibrarySubobject.SetDXILLibrary(&dxilLibrary);

    CD3DX12_HIT_GROUP_SUBOBJECT primaryHitGroupSubobject(stateObject);
    primaryHitGroupSubobject.SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    primaryHitGroupSubobject.SetHitGroupExport(L"PrimaryHitGroup");
    primaryHitGroupSubobject.SetAnyHitShaderImport(L"PrimaryAnyHit");
    primaryHitGroupSubobject.SetClosestHitShaderImport(L"PrimaryClosestHit");

    CD3DX12_HIT_GROUP_SUBOBJECT secondaryHitGroupSubobject(stateObject);
    secondaryHitGroupSubobject.SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    secondaryHitGroupSubobject.SetHitGroupExport(L"SecondaryHitGroup");
    secondaryHitGroupSubobject.SetAnyHitShaderImport(L"SecondaryAnyHit");
    secondaryHitGroupSubobject.SetClosestHitShaderImport(L"SecondaryClosestHit");

    CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT shaderConfigSubobject(stateObject);
    shaderConfigSubobject.Config(sizeof(float) * 5, sizeof(float) * 2);

    CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT pipelineConfigSubobject(stateObject);
    pipelineConfigSubobject.Config(3);

    CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT rootSignatureSubobject(stateObject);
    rootSignatureSubobject.SetRootSignature(m_raytracingRootSignature.Get());

    HRESULT hr = m_device->CreateStateObject(stateObject, IID_PPV_ARGS(m_stateObject.GetAddressOf()));
    assert(SUCCEEDED(hr) && m_stateObject != nullptr);

    ComPtr<ID3D12StateObjectProperties> properties;
    hr = m_stateObject.As(std::addressof(properties));

    assert(SUCCEEDED(hr) && properties != nullptr);

    m_shaderTable.resize(12 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    memcpy(&m_shaderTable[0 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES], properties->GetShaderIdentifier(L"PrimaryRayGeneration"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    memcpy(&m_shaderTable[2 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES], properties->GetShaderIdentifier(L"ShadowRayGeneration"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    memcpy(&m_shaderTable[4 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES], properties->GetShaderIdentifier(L"GlobalIlluminationRayGeneration"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    memcpy(&m_shaderTable[6 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES], properties->GetShaderIdentifier(L"ReflectionRayGeneration"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    memcpy(&m_shaderTable[8 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES], properties->GetShaderIdentifier(L"PrimaryMiss"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    memcpy(&m_shaderTable[9 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES], properties->GetShaderIdentifier(L"SecondaryMiss"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    memcpy(&m_shaderTable[10 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES], properties->GetShaderIdentifier(L"PrimaryHitGroup"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    memcpy(&m_shaderTable[11 * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES], properties->GetShaderIdentifier(L"SecondaryHitGroup"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    for (auto& scratchBuffer : m_scratchBuffers)
    {
        D3D12MA::ALLOCATION_DESC allocDesc{};
        allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
        allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;

        const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(
            SCRATCH_BUFFER_SIZE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        HRESULT hr = m_allocator->CreateResource(
            &allocDesc,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            scratchBuffer.GetAddressOf(),
            IID_ID3D12Resource,
            nullptr);

        assert(SUCCEEDED(hr) && scratchBuffer != nullptr);
    }

    CD3DX12_DESCRIPTOR_RANGE1 copyDescriptorRanges[1];
    copyDescriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

    CD3DX12_ROOT_PARAMETER1 copyRootParams[1];
    copyRootParams[0].InitAsDescriptorTable(_countof(copyDescriptorRanges), copyDescriptorRanges, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC copyStaticSamplers[1]{};
    copyStaticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    copyStaticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    copyStaticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    copyStaticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    RootSignature::create(
        m_device.Get(),
        copyRootParams,
        _countof(copyRootParams),
        copyStaticSamplers,
        _countof(copyStaticSamplers),
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS,
        m_copyTextureRootSignature);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = m_copyTextureRootSignature.Get();
    pipelineDesc.VS.pShaderBytecode = CopyTextureVertexShader;
    pipelineDesc.VS.BytecodeLength = sizeof(CopyTextureVertexShader);
    pipelineDesc.PS.pShaderBytecode = CopyTexturePixelShader;
    pipelineDesc.PS.BytecodeLength = sizeof(CopyTexturePixelShader);
    pipelineDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pipelineDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pipelineDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    pipelineDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    pipelineDesc.SampleMask = ~0;
    pipelineDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipelineDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineDesc.NumRenderTargets = 1;
    pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pipelineDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pipelineDesc.SampleDesc.Count = 1;

    hr = m_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(m_copyTexturePipeline.GetAddressOf()));

    assert(SUCCEEDED(hr) && m_copyTexturePipeline != nullptr);

    CD3DX12_ROOT_PARAMETER1 poseRootParams[4];
    poseRootParams[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    poseRootParams[1].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    poseRootParams[2].InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    poseRootParams[3].InitAsUnorderedAccessView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE);

    RootSignature::create(
        m_device.Get(),
        poseRootParams,
        _countof(poseRootParams),
        nullptr,
        0,
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS,
        m_poseRootSignature);

    D3D12_COMPUTE_PIPELINE_STATE_DESC posePipelineDesc{};
    posePipelineDesc.pRootSignature = m_poseRootSignature.Get();
    posePipelineDesc.CS.pShaderBytecode = PoseComputeShader;
    posePipelineDesc.CS.BytecodeLength = sizeof(PoseComputeShader);

    hr = m_device->CreateComputePipelineState(&posePipelineDesc, IID_PPV_ARGS(m_posePipeline.GetAddressOf()));

    assert(SUCCEEDED(hr) && m_posePipeline != nullptr);

    const INIReader reader("GenerationsRaytracing.ini");
    if (reader.ParseError() == 0)
    {
        m_qualityMode = static_cast<QualityMode>(
            reader.GetInteger("Mod", "QualityMode", static_cast<long>(QualityMode::Balanced)));
    }

    m_upscaler = std::make_unique<DLSS>(*this);

    D3D12_COMPUTE_PIPELINE_STATE_DESC resolvePipelineDesc{};
    resolvePipelineDesc.pRootSignature = m_raytracingRootSignature.Get();
    resolvePipelineDesc.CS.pShaderBytecode = ResolveComputeShader;
    resolvePipelineDesc.CS.BytecodeLength = sizeof(ResolveComputeShader);

    hr = m_device->CreateComputePipelineState(&resolvePipelineDesc, IID_PPV_ARGS(m_resolvePipeline.GetAddressOf()));

    assert(SUCCEEDED(hr) && m_resolvePipeline != nullptr);
}
