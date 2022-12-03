﻿#pragma once

class MessageCallback;
struct Window;

struct Device
{
    struct
    {
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> graphicsCommandQueue;
        ComPtr<ID3D12CommandQueue> computeCommandQueue;
        ComPtr<ID3D12CommandQueue> copyCommandQueue;
    } d3d12;

    ComPtr<IDXGIFactory4> dxgiFactory;
    std::unique_ptr<MessageCallback> messageCallback;
    nvrhi::DeviceHandle nvrhi;

    Device();
    ~Device();
};