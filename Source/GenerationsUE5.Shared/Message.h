#pragma once

#include <cstdint>

#define MSG_DEFINE_MESSAGE(PREVIOUS_MESSAGE) \
    static constexpr uint8_t s_id = PREVIOUS_MESSAGE::s_id + 1; \
    uint8_t id = s_id

#pragma pack(push, 1)

struct MsgPadding
{
    static constexpr uint32_t s_id = 0;
    uint8_t id;
    uint8_t dataSize;
    uint8_t data[1u];
};

struct MsgCreateSwapChain
{
    MSG_DEFINE_MESSAGE(MsgPadding);
    uint32_t postHandle;
    uint32_t style;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint16_t renderWidth;
    uint16_t renderHeight;
    uint8_t format;
    uint8_t bufferCount;
    bool hdr;
    uint8_t syncInterval;
    uint32_t textureId;
};

struct MsgSetRenderTarget
{
    MSG_DEFINE_MESSAGE(MsgCreateSwapChain);
    uint8_t renderTargetIndex;
    uint32_t textureId;
    uint8_t textureLevel;
};
struct MsgCreateVertexDeclaration
{
    MSG_DEFINE_MESSAGE(MsgSetRenderTarget);
    uint32_t vertexDeclarationId;
    bool isFVF;
    uint16_t dataSize;
    uint8_t data[1u];
};
struct MsgCreatePixelShader
{
    MSG_DEFINE_MESSAGE(MsgCreateVertexDeclaration);
    uint32_t pixelShaderId;
    uint32_t dataSize;
    alignas(0x10) uint8_t data[1u];
};
struct MsgCreateVertexShader
{
    MSG_DEFINE_MESSAGE(MsgCreatePixelShader);
    uint32_t vertexShaderId;
    uint32_t dataSize;
    alignas(0x10) uint8_t data[1u];
};

struct MsgSetRenderState
{
    MSG_DEFINE_MESSAGE(MsgCreateVertexShader);
    uint8_t state;
    uint32_t value;
};

struct MsgCreateTexture
{
    MSG_DEFINE_MESSAGE(MsgSetRenderState);
    uint16_t width;
    uint16_t height;
    uint8_t levels;
    uint32_t usage;
    uint32_t format;
    uint32_t textureId;
};

struct MsgSetTexture
{
    MSG_DEFINE_MESSAGE(MsgCreateTexture);
    uint8_t stage;
    uint32_t textureId;
};

struct MsgSetDepthStencilSurface
{
    MSG_DEFINE_MESSAGE(MsgSetTexture);
    uint32_t depthStencilSurfaceId;
    uint8_t level;
};

struct MsgClear
{
    MSG_DEFINE_MESSAGE(MsgSetDepthStencilSurface);
    uint32_t flags;
    uint32_t color;
    float z;
    uint8_t stencil;
};

struct MsgSetVertexShader
{
    MSG_DEFINE_MESSAGE(MsgClear);
    uint32_t vertexShaderId;
};

struct MsgSetPixelShader
{
    MSG_DEFINE_MESSAGE(MsgSetVertexShader);
    uint32_t pixelShaderId;
};

struct MsgSetPixelShaderConstantF
{
    MSG_DEFINE_MESSAGE(MsgSetPixelShader);
    uint8_t startRegister;
    uint16_t dataSize;
    uint8_t data[1u];
};

struct MsgSetVertexShaderConstantF
{
    MSG_DEFINE_MESSAGE(MsgSetPixelShaderConstantF);
    uint8_t startRegister;
    uint16_t dataSize;
    uint8_t data[1u];
};

struct MsgSetVertexShaderConstantB
{
    MSG_DEFINE_MESSAGE(MsgSetVertexShaderConstantF);
    uint32_t startRegister;
    uint8_t dataSize;
    uint8_t data[1u];
};

struct MsgSetSamplerState
{
    MSG_DEFINE_MESSAGE(MsgSetVertexShaderConstantB);
    uint8_t sampler;
    uint8_t type;
    uint32_t value;
};

struct MsgSetViewport
{
    MSG_DEFINE_MESSAGE(MsgSetSamplerState);
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    float minZ;
    float maxZ;
};

struct MsgSetScissorRect
{
    MSG_DEFINE_MESSAGE(MsgSetViewport);
    uint16_t left;
    uint16_t top;
    uint16_t right;
    uint16_t bottom;
};

struct MsgSetVertexDeclaration
{
    MSG_DEFINE_MESSAGE(MsgSetScissorRect);
    uint32_t vertexDeclarationId;
};

struct MsgDrawPrimitiveUP
{
    MSG_DEFINE_MESSAGE(MsgSetVertexDeclaration);
    uint8_t primitiveType;
    uint32_t vertexCount;
    uint8_t vertexStreamZeroStride;
    uint32_t dataSize;
    uint8_t data[1u];
};

struct MsgSetStreamSource
{
    MSG_DEFINE_MESSAGE(MsgDrawPrimitiveUP);
    uint8_t streamNumber;
    uint32_t streamDataId;
    uint32_t offsetInBytes;
    uint8_t stride;
};

struct MsgSetIndices
{
    MSG_DEFINE_MESSAGE(MsgSetStreamSource);
    uint32_t indexDataId;
};

struct MsgPresent
{
    MSG_DEFINE_MESSAGE(MsgSetIndices);
};

struct MsgCreateVertexBuffer
{
    MSG_DEFINE_MESSAGE(MsgPresent);
    uint32_t length;
    uint32_t vertexBufferId;
    bool allowUnorderedAccess;
};

struct MsgWriteVertexBuffer
{
    MSG_DEFINE_MESSAGE(MsgCreateVertexBuffer);
    uint32_t vertexBufferId;
    uint32_t offset;
    bool initialWrite;
    uint32_t dataSize;
    alignas(0x10) uint8_t data[1u];
};

struct MsgCreateIndexBuffer
{
    MSG_DEFINE_MESSAGE(MsgWriteVertexBuffer);
    uint32_t length;
    uint32_t format;
    uint32_t indexBufferId;
};

struct MsgWriteIndexBuffer
{
    MSG_DEFINE_MESSAGE(MsgCreateIndexBuffer);
    uint32_t indexBufferId;
    uint32_t offset;
    bool initialWrite;
    uint32_t dataSize;
    alignas(0x10) uint8_t data[1u];
};

struct MsgWriteTexture
{
    MSG_DEFINE_MESSAGE(MsgWriteIndexBuffer);
    uint32_t textureId;
    uint32_t width;
    uint32_t height;
    uint32_t level;
    uint32_t pitch;
    uint32_t dataSize;
    alignas(0x10) uint8_t data[1u];
};

struct MsgMakeTexture
{
    MSG_DEFINE_MESSAGE(MsgWriteTexture);
    uint32_t textureId;
#ifdef _DEBUG
    char textureName[0x100];
#endif
    uint32_t dataSize;
    alignas(0x10) uint8_t data[1u];
};

struct MsgDrawIndexedPrimitive
{
    MSG_DEFINE_MESSAGE(MsgMakeTexture);
    uint8_t primitiveType;
    int32_t baseVertexIndex;
    uint32_t startIndex;
    uint32_t indexCount;
};

struct MsgSetStreamSourceFreq
{
    MSG_DEFINE_MESSAGE(MsgDrawIndexedPrimitive);
    uint8_t streamNumber;
    uint32_t setting;
};

struct MsgReleaseResource
{
    MSG_DEFINE_MESSAGE(MsgSetStreamSourceFreq);

    enum class ResourceType : uint8_t
    {
        Texture,
        IndexBuffer,
        VertexBuffer,
    };

    ResourceType resourceType;
    uint32_t resourceId;
};

struct MsgDrawPrimitive
{
    MSG_DEFINE_MESSAGE(MsgReleaseResource);
    uint8_t primitiveType;
    uint32_t startVertex;
    uint32_t vertexCount;
};

struct MsgCreateBottomLevelAccelStruct
{
    MSG_DEFINE_MESSAGE(MsgDrawPrimitive);

    struct GeometryDesc
    {
        uint32_t flags;
        uint32_t indexBufferId;
        uint32_t indexCount;
        uint32_t indexOffset;
        uint32_t vertexBufferId;
        uint32_t vertexStride;
        uint32_t vertexCount;
        uint32_t vertexOffset;
        uint32_t normalOffset;
        uint32_t tangentOffset;
        uint32_t binormalOffset;
        uint32_t colorOffset;
        uint32_t texCoordOffsets[4];
        uint32_t materialId;
    };

    uint32_t bottomLevelAccelStructId;
    bool allowUpdate;
    bool allowCompaction;
    bool preferFastBuild;
    bool asyncBuild;
    uint32_t dataSize;
    uint8_t data[1u];
};

enum class RaytracingResourceType : uint8_t
{
    BottomLevelAccelStruct,
    Material
};

struct MsgReleaseRaytracingResource
{
    MSG_DEFINE_MESSAGE(MsgCreateBottomLevelAccelStruct);

    RaytracingResourceType resourceType;
    uint32_t resourceId;
};

struct MsgCreateInstance
{
    MSG_DEFINE_MESSAGE(MsgReleaseRaytracingResource);

    float transform[3][4];
    float prevTransform[3][4];
    float headTransform[3][4];
    uint32_t bottomLevelAccelStructId;
    bool isMirrored;
    uint8_t instanceMask;
    uint8_t instanceType;
    float playableParam;
    float chrPlayableMenuParam;
    float forceAlphaColor;
    float edgeEmissionParam;
    uint32_t dataSize;
    uint8_t data[1u];
};

struct MsgTraceRays
{
    MSG_DEFINE_MESSAGE(MsgCreateInstance);

    uint16_t width;
    uint16_t height;
    bool resetAccumulation;
    float diffusePower;
    float lightPower;
    float emissivePower;
    float skyPower;
    uint32_t debugView;
    uint32_t envMode;
    float skyColor[3];
    float groundColor[3];
    bool useSkyTexture;
    float backgroundColor[3];
    uint32_t upscaler;
    uint32_t qualityMode;
    uint32_t adaptionLuminanceTextureId;
    float middleGray;
    bool skyInRoughReflection;
    bool enableExposureTexture;
    uint32_t envBrdfTextureId;
};

struct MsgCreateMaterial
{
    MSG_DEFINE_MESSAGE(MsgTraceRays);

    struct Texture
    {
        uint32_t id;
        uint8_t addressModeU;
        uint8_t addressModeV;
        uint32_t texCoordIndex;
    };

    uint32_t materialId;
    uint32_t shaderType;
    uint32_t flags;
    float texCoordOffsets[8];
    uint32_t textureCount;
    Texture textures[16];
    uint32_t parameterCount;
    float parameters[32];
};

struct MsgComputePose
{
    MSG_DEFINE_MESSAGE(MsgCreateMaterial);

    struct GeometryDesc
    {
        uint32_t vertexCount;
        uint32_t vertexBufferId;
        uint32_t vertexOffset;
        uint8_t vertexStride;
        uint8_t normalOffset;
        uint8_t tangentOffset;
        uint8_t binormalOffset;
        uint8_t blendWeightOffset;
        uint8_t blendIndicesOffset;
        uint8_t blendWeight1Offset;
        uint8_t blendIndices1Offset;
        uint8_t nodeCount;
        bool visible;
    };

    uint32_t vertexBufferId;
    uint8_t nodeCount;
    uint32_t geometryCount;
    uint32_t dataSize;
    uint8_t data[1u];
};

struct MsgBuildBottomLevelAccelStruct
{
    MSG_DEFINE_MESSAGE(MsgComputePose);
    uint32_t bottomLevelAccelStructId;
    bool performUpdate;
};

struct MsgCopyVertexBuffer
{
    MSG_DEFINE_MESSAGE(MsgBuildBottomLevelAccelStruct);
    uint32_t dstVertexBufferId;
    uint32_t dstOffset;
    uint32_t srcVertexBufferId;
    uint32_t srcOffset;
    uint32_t numBytes;
};

struct MsgRenderSky
{
    MSG_DEFINE_MESSAGE(MsgCopyVertexBuffer);

    struct GeometryDesc
    {
        uint32_t flags;
        uint32_t vertexBufferId;
        uint32_t vertexOffset;
        uint32_t vertexStride;
        uint32_t vertexCount;
        uint32_t indexBufferId;
        uint32_t indexOffset;
        uint32_t indexCount;
        uint32_t vertexDeclarationId;
        bool isAdditive;
        bool enableVertexColor;
        MsgCreateMaterial::Texture diffuseTexture;
        MsgCreateMaterial::Texture alphaTexture;
        MsgCreateMaterial::Texture emissionTexture;
        float diffuse[4];
        float ambient[4];
    };

    float backgroundScale;
    uint32_t dataSize;
    uint8_t data[1u];
};

struct MsgCreateLocalLight
{
    MSG_DEFINE_MESSAGE(MsgRenderSky);

    float position[3];
    float color[3];
    float inRange;
    float outRange;
    bool castShadow;
    bool enableBackfaceCulling;
    float shadowRange;
};

struct MsgSetPixelShaderConstantB
{
    MSG_DEFINE_MESSAGE(MsgCreateLocalLight);
    uint32_t startRegister;
    uint8_t dataSize;
    uint8_t data[1u];
};

struct MsgSaveShaderCache
{
    MSG_DEFINE_MESSAGE(MsgSetPixelShaderConstantB);
};

struct MsgComputeSmoothNormal
{
    MSG_DEFINE_MESSAGE(MsgSaveShaderCache);
    uint32_t indexBufferId;
    uint32_t indexOffset;
    uint8_t vertexStride;
    uint32_t vertexCount;
    uint32_t vertexOffset;
    uint8_t normalOffset;
    uint32_t vertexBufferId;
    uint32_t adjacencyBufferId;
    bool isMirrored;
};

struct MsgDrawIndexedPrimitiveUP
{
    MSG_DEFINE_MESSAGE(MsgComputeSmoothNormal);
    uint8_t primitiveType;
    uint32_t vertexCount;
    uint32_t indexCount;
    uint8_t indexFormat;
    uint8_t vertexStride;
    uint32_t dataSize;
    uint8_t data[1u];
};

struct MsgShowCursor
{
    MSG_DEFINE_MESSAGE(MsgDrawIndexedPrimitiveUP);
    bool showCursor;
};

struct MsgDispatchUpscaler
{
    MSG_DEFINE_MESSAGE(MsgShowCursor);
    uint32_t debugView;
    bool resetAccumulation;
};

struct MsgDrawIm3d
{
    MSG_DEFINE_MESSAGE(MsgDispatchUpscaler);

    struct DrawList
    {
        uint8_t primitiveType;
        uint32_t vertexOffset;
        uint32_t vertexCount;
    };

    float projection[4][4];
    float view[4][4];
    float viewportSize[2];
    uint32_t depthStencilId;
    uint32_t vertexSize;
    uint32_t drawListCount;
    uint32_t dataSize;
    uint8_t data[1u];
};

struct MsgCopyHdrTexture
{
    MSG_DEFINE_MESSAGE(MsgDrawIm3d);
};

struct MsgComputeGrassInstancer
{
    MSG_DEFINE_MESSAGE(MsgCopyHdrTexture);

    struct LodDesc
    {
        uint32_t instanceOffset;
        uint32_t instanceCount;
        uint32_t vertexOffset;
    };

    float instanceTypes[32][4];
    float misc;
    uint32_t instanceBufferId;
    uint32_t vertexBufferId;
    uint32_t dataSize;
    uint8_t data[1u];
};

struct MsgSonicInit
{
    MSG_DEFINE_MESSAGE(MsgComputeGrassInstancer);

    bool bIsModern;
};

struct MsgSonicUpdate
{
    MSG_DEFINE_MESSAGE(MsgSonicInit);
    
    bool bIsSuper;

    float matrix[16];

    int boneCount = 0;
    char boneNames[0xFF][0x40];
    float boneMatrices[0xFF][0x10];
};

struct MsgCameraUpdate
{
    MSG_DEFINE_MESSAGE(MsgSonicUpdate);
    
    float posX;
    float posY;
    float posZ;
    
    float tgtX;
    float tgtY;
    float tgtZ;
    
    float upX;
    float upY;
    float upZ;

    float fov;
    float aspectRatio;
};

#pragma pack(pop)