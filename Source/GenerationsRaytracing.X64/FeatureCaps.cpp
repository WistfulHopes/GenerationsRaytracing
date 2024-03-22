#include "FeatureCaps.h"

extern "C"
{
    __declspec(dllexport) extern const UINT D3D12SDKVersion = D3D12_SDK_VERSION;
    __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\";

    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

bool FeatureCaps::ensureMinimumCapability(ID3D12Device* device, bool& gpuUploadHeapSupported)
{
    CD3DX12FeatureSupport features;
    features.Init(device);

    const bool result = features.RaytracingTier() >= D3D12_RAYTRACING_TIER_1_1 &&
        features.HighestShaderModel() >= D3D_SHADER_MODEL_6_6 &&
        features.ResourceBindingTier() >= D3D12_RESOURCE_BINDING_TIER_2 &&
        features.HighestRootSignatureVersion() >= D3D_ROOT_SIGNATURE_VERSION_1_1;

    gpuUploadHeapSupported = features.GPUUploadHeapSupported();

    return result;
}
