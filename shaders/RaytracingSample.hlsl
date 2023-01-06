#ifndef RAYTRACING_SAMPLE_HLSL
#define RAYTRACING_SAMPLE_HLSL

struct RaytracingSampleConstantBuffer
{
    float4x4 projectionToWorld;
    float4 cameraPosition;
};

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);
ConstantBuffer<RaytracingSampleConstantBuffer> g_rayGenCB : register(b0);

struct VertexAttributes
{
    float3 position;
    float3 normal;
    float3 tangent;
    float2 uv;
};
Texture2D<float4> tex_diffuse : register(t1);
ByteAddressBuffer Vertices : register(t2);
ByteAddressBuffer Indices : register(t3);

SamplerState ss : register(s0);

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
    float4 color;
};

inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    float2 xy = index + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a ray.
    float4 world = mul(g_rayGenCB.projectionToWorld, float4(screenPos, 0, 1));

    world.xyz /= world.w;
    origin = g_rayGenCB.cameraPosition.xyz;
    direction = normalize(world.xyz - origin);
}

[shader("raygeneration")]
void MyRaygenShader()
{
    float3 rayDir;
    float3 origin;

    // Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
    GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

    // Trace the ray.
    // Set the ray's extents.
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = rayDir;
    // Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
    // TMin should be kept small to prevent missing geometry at close contact areas.
    ray.TMin = 0.001;
    ray.TMax = 100.0;
    RayPayload payload = { float4(0, 1, 0, 0) };
    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

    // Write the raytraced color to the output texture.
    RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

// ************************************************
uint3 GetIndices(uint offsetBytes)
{
    uint3 indices;

    // ByteAdressBuffer loads must be aligned at a 4 byte boundary.
    // Since we need to read three 16 bit indices: { 0, 1, 2 } 
    // aligned at a 4 byte boundary as: { 0 1 } { 2 0 } { 1 2 } { 0 1 } ...
    // we will load 8 bytes (~ 4 indices { a b | c d }) to handle two possible index triplet layouts,
    // based on first index's offsetBytes being aligned at the 4 byte boundary or not:
    //  Aligned:     { 0 1 | 2 - }
    //  Not aligned: { - 0 | 1 2 }
    const uint dwordAlignedOffset = offsetBytes & ~3;
    const uint2 four16BitIndices = Indices.Load2(dwordAlignedOffset);

    // Aligned: { 0 1 | 2 - } => retrieve first three 16bit indices
    if (dwordAlignedOffset == offsetBytes)
    {
        indices.x = four16BitIndices.x & 0xffff;
        indices.y = (four16BitIndices.x >> 16) & 0xffff;
        indices.z = four16BitIndices.y & 0xffff;
    } else // Not aligned: { - 0 | 1 2 } => retrieve last three 16bit indices
    {
        indices.x = (four16BitIndices.x >> 16) & 0xffff;
        indices.y = four16BitIndices.y & 0xffff;
        indices.z = (four16BitIndices.y >> 16) & 0xffff;
    }

    return indices;
}

VertexAttributes GetVertexAttributes(uint triangleIndex, float3 barycentrics)
{
    uint indexSizeInBytes = 2;
    uint indicesPerTriangle = 3;
    uint triangleIndexStride = indicesPerTriangle * indexSizeInBytes;
    uint baseIndex = triangleIndex * triangleIndexStride;

    uint3 primitive_indices = GetIndices(baseIndex);
    VertexAttributes v;
    v.position = float3(0, 0, 0);
    v.normal = float3(0, 0, 0);
    v.tangent = float3(0, 0, 0);
    v.uv = float2(0, 0);

    for (uint i = 0; i < 3; i++)
    {
        int address = primitive_indices[i] * 11 * 4;
        v.position += asfloat(Vertices.Load3(address)) * barycentrics[i];
        address += (3 * 4);
        
        v.normal += asfloat(Vertices.Load3(address)) * barycentrics[i];
        address += (3 * 4);

        v.tangent += asfloat(Vertices.Load3(address)) * barycentrics[i];
        address += (3 * 4);
        
        v.uv += asfloat(Vertices.Load2(address)) * barycentrics[i];
    }

    return v;
}
// ************************************************

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attrib)
{
    uint triangleIndex = PrimitiveIndex();	
    float3 barycentrics = float3((1.0f - attrib.barycentrics.x - attrib.barycentrics.y), attrib.barycentrics.x, attrib.barycentrics.y);
    VertexAttributes vertex = GetVertexAttributes(triangleIndex, barycentrics);

    payload.color = float4(tex_diffuse.SampleLevel(ss, vertex.uv, 0).rgb, 1.f);
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    payload.color = float4( 0.0f, 0.4f, 0.2f, 1.0f );
}

#endif // RAYTRACING_HLSL