#define FLT_MAX asfloat(0x7f7fffff)

struct Payload
{
    float4 color : COLOR;
    bool miss : MISS;
};

struct Attributes
{
    float2 uv;
};

struct cbGlobals
{
    float3 position;
    float tanFovy;
    float4x4 rotation;
    float aspectRatio;
};

ConstantBuffer<cbGlobals> g_Globals : register(b0);

RaytracingAccelerationStructure g_BVH : register(t0);
Buffer<uint3> g_MeshBuffer : register(t1);
Buffer<float3> g_NormalBuffer : register(t2);
Buffer<float4> g_TangentBuffer : register(t3);
Buffer<float2> g_TexCoordBuffer : register(t4);
Buffer<float4> g_ColorBuffer : register(t5);
Buffer<uint> g_IndexBuffer : register(t6);

RWTexture2D<float4> g_Output : register(u0);

[shader("raygeneration")]
void RayGeneration()
{
    uint2 index = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    float2 ndc = (index + 0.5) / dimensions * 2.0 - 1.0;

    RayDesc ray;
    ray.Origin = g_Globals.position;
    ray.Direction = normalize(mul(g_Globals.rotation, float4(ndc.x * g_Globals.tanFovy * g_Globals.aspectRatio, -ndc.y * g_Globals.tanFovy, -1.0, 0.0)).xyz);
    ray.TMin = 0.001f;
    ray.TMax = FLT_MAX;

    Payload payload = (Payload)0;
    TraceRay(g_BVH, 0, 1, 0, 1, 0, ray, payload);

    g_Output[index] = payload.color;
}

[shader("miss")]
void Miss(inout Payload payload : SV_RayPayload)
{
    payload.color = 0.0;
    payload.miss = true;
}

[shader("closesthit")]
void ClosestHit(inout Payload payload : SV_RayPayload, Attributes attributes : SV_IntersectionAttributes)
{
    const float3 lightDirection = normalize(float3(0.5, 1, 0));

    float3 position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

    RayDesc shadowRay;
    shadowRay.Origin = position;
    shadowRay.Direction = lightDirection;
    shadowRay.TMin = 0.01f;
    shadowRay.TMax = FLT_MAX;

    Payload shadowPayload = (Payload)0;
    TraceRay(g_BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 1, 0, 1, 0, shadowRay, shadowPayload);

    uint3 mesh = g_MeshBuffer[InstanceID() + GeometryIndex()];

    uint3 indices;
    indices.x = mesh.x + g_IndexBuffer[mesh.y + PrimitiveIndex() * 3 + 0];
    indices.y = mesh.x + g_IndexBuffer[mesh.y + PrimitiveIndex() * 3 + 1];
    indices.z = mesh.x + g_IndexBuffer[mesh.y + PrimitiveIndex() * 3 + 2];

    float3 uv = float3(1.0 - attributes.uv.x - attributes.uv.y, attributes.uv.x, attributes.uv.y);

    float3 normal = 
        g_NormalBuffer[indices.x] * uv.x + 
        g_NormalBuffer[indices.y] * uv.y + 
        g_NormalBuffer[indices.z] * uv.z;

    normal = normalize(mul(ObjectToWorld3x4(), float4(normal, 0.0))).xyz;

    payload.color.rgb = saturate(dot(normal, lightDirection)) * (shadowPayload.miss ? 1.0 : 0.0) + 0.25;
    payload.color.a = 1.0;
}