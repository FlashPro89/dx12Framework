
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


struct PSInput                         
{                                      
    float4 position : SV_POSITION;     
    float2 uv : TEXCOORD0;                  
};                                     

Texture2D g_texture : register(t0);           
SamplerState g_sampler : register(s0);     

float4 ps_main(PSInput input) : SV_TARGET  
{                                          
	return g_texture.Sample(g_sampler, input.uv); 
} ;