﻿#pragma once

struct Device;

struct Texture
{
    std::unique_ptr<uint8_t[]> data;
    size_t dataSize = 0;
};

struct Material
{
    Eigen::Vector4f float4Parameters[16] {};
    uint32_t textureIndices[16] {};
};

struct Mesh
{
    enum class Type
    {
        Opaque,
        Trans,
        Punch,
        Special
    };

    Type type = Type::Opaque;
    uint32_t vertexOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;

    struct GPU
    {
        uint32_t vertexOffset;
        uint32_t indexOffset;
        uint32_t materialIndex;
    };
};

struct Model
{
    uint32_t meshOffset = 0;
    uint32_t meshCount = 0;
};

struct Instance
{
    Eigen::Matrix4f transform;
    uint32_t modelIndex = 0;

    Instance()
        : transform(Eigen::Matrix4f::Identity())
    {
        
    }
};

struct Scene
{
    struct CPU
    {
        std::vector<Texture> textures;
        std::vector<Material> materials;

        std::vector<Mesh> meshes;
        std::vector<Model> models;
        std::vector<Instance> instances;

        std::vector<Eigen::Vector3f> vertices;
        std::vector<Eigen::Vector3f> normals;
        std::vector<Eigen::Vector4f> tangents;
        std::vector<Eigen::Vector2f> texCoords;
        std::vector<Eigen::Vector4f> colors;
        std::vector<uint16_t> indices;
    } cpu;

    struct GPU
    {
        std::vector<nvrhi::rt::AccelStructHandle> bottomLevelAccelStructs;
        nvrhi::rt::AccelStructHandle topLevelAccelStruct;

        nvrhi::BufferHandle meshBuffer;
        nvrhi::BufferHandle vertexBuffer;
        nvrhi::BufferHandle normalBuffer;
        nvrhi::BufferHandle tangentBuffer;
        nvrhi::BufferHandle texCoordBuffer;
        nvrhi::BufferHandle colorBuffer;
        nvrhi::BufferHandle indexBuffer;
    } gpu;

    void loadCpuResources(const std::string& directoryPath);
    void createGpuResources(const Device& device);
};