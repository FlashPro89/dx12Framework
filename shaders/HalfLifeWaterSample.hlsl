
struct VS_INPUT                    
{                                   
    float3 pos : POSITION;          
    float3 normal : NORMAL;          
    float3 tangent : TANGENT;      
    float2 texCoord : TEXCOORD;     
};                                 

struct VS_OUTPUT                       
{                                      
    float4 pos : SV_POSITION;          
    float2 texCoord : TEXCOORD0;        
}; 

struct sConstantBuffer                 
{                                      
    float4x4 wvpMat;
};  

ConstantBuffer<sConstantBuffer> myCBuffer : register(b0); 
VS_OUTPUT vs_main(VS_INPUT input)         
{                                      
    VS_OUTPUT output; 
    output.pos = mul( float4( input.pos, 1.0f), myCBuffer.wvpMat );
    output.texCoord = input.texCoord;   
    return output;                          
};


struct sConstantBufferPS
{
    float4 time;
};

ConstantBuffer<sConstantBufferPS> myCBufferPS : register(b0);

struct PSInput                         
{                                      
    float4 position : SV_POSITION;     
    float2 uv : TEXCOORD0;                  
};                                     

Texture2D g_texture : register(t0);           
SamplerState g_sampler : register(s0);     

float4 ps_main(PSInput input) : SV_TARGET
{
    float amplitude = 0.1f;
    float frequency = 2.5f;
    float speed = 150.f;

    float2 uv;
    uv.x = input.uv.x + amplitude * sin(input.uv.y * frequency + myCBufferPS.time.x * speed);
    uv.y = input.uv.y + amplitude * sin(input.uv.x * frequency + myCBufferPS.time.x * speed + 3.1415 / 2);

	return g_texture.Sample(g_sampler, uv); 

    // debug: show uv
    return float4(fmod(uv.x, 1), fmod(uv.y, 1), 0.f, 1.f);
} ;