
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
    float4 invLightDirTS : TEXCOORD1;       
    float3 normal : TEXCOORD2;          
    float3 binormal : TEXCOORD3;          
    float3 tangent : TEXCOORD4;          
    float3 viewDirTS : TEXCOORD5;       
}; 

struct sConstantBuffer                 
{                                      
    float4x4 wvpMat;                   
    float4x4 wMat;                     
    float4 lightDir;                   
    float3 viewDir;                    
};  

ConstantBuffer<sConstantBuffer> myCBuffer : register(b0); 
VS_OUTPUT vs_main(VS_INPUT input)         
{                                      
    VS_OUTPUT output; 
    output.pos = mul( float4( input.pos, 1.0f), myCBuffer.wvpMat );
    output.texCoord = input.texCoord;  

    output.normal = normalize( mul( input.normal, (float3x3)myCBuffer.wMat ) ).xyz; 
    output.tangent = output.tangent = normalize( mul( input.tangent, (float3x3)myCBuffer.wMat ) ).xyz; 
    output.binormal = normalize( cross( output.tangent, output.normal ) ); 

    float3x3 mTangentSpace = float3x3( output.tangent, output.binormal, output.normal ); 
    output.invLightDirTS.xyz = mul( -normalize( myCBuffer.lightDir.xyz ), mTangentSpace );   
    output.invLightDirTS.w = myCBuffer.lightDir.w;   
    output.viewDirTS = mul( mTangentSpace, normalize( myCBuffer.viewDir ) );     
           
    return output;                          
};


struct PSInput                         
{                                      
    float4 position : SV_POSITION;     
    float2 uv : TEXCOORD0;              
    float4 invLightDirTS : TEXCOORD1;       
    float3 normal : TEXCOORD2;          
    float3 binormal : TEXCOORD3;          
    float3 tangent : TEXCOORD4;          
    float3 viewDirTS : TEXCOORD5;       
};                                     

Texture2D g_texture : register(t0);       
Texture2D g_normMap : register(t1);       
SamplerState g_sampler : register(s0);     

// lightVec.w = ambient intensivity 
float4 ps_main(PSInput input) : SV_TARGET  
{                                          
// Simple Parallax Mapping implementation: 

    float3 viewDirTS = normalize(input.viewDirTS); 
    float3 invLightDirTS = normalize(input.invLightDirTS.xyz); 
    const float sfHeightBias = -0.005f;      
    const float sfHeightScale = 0.02f;     
    float fCurrentHeight = g_normMap.Sample(g_sampler, input.uv).w;      
    float fHeight = saturate(fCurrentHeight * sfHeightScale + sfHeightBias);      
    viewDirTS.y = -viewDirTS.y;
    fHeight /= min(viewDirTS.z, -0.25f);      
    float2 texSample = input.uv + viewDirTS.xy * fHeight;      

    float4 diffuseSample = g_texture.Sample(g_sampler, texSample); 
    float4 normalSample =  g_normMap.Sample(g_sampler, texSample); 
    normalSample.xyz =  normalSample.xyz  * 2.f - 1.f;        
    float power = 48; 
    float4 ambientComponent = float4(0.1f, 0.1f, 0.1f, 1.f);   
    float4 diffuseComponent = saturate( dot( normalSample.xyz, invLightDirTS.xyz) ); 
    float4 specularComponent = pow( saturate( dot( reflect( viewDirTS, normalSample.xyz ), invLightDirTS.xyz ) ), power ); 
    return ambientComponent + diffuseComponent * diffuseSample + specularComponent * float4(1.f, 1.f, 1.f, 1.f);
} ;