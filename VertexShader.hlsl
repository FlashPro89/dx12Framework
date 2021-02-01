struct VS_INPUT
{
	float4 pos : POSITION;
	float2 texCoord: TEXCOORD;
	float4 color : COLOR;
};

struct VS_OUTPUT
{
	float4 pos: SV_POSITION;
	float2 texCoord: TEXCOORD;
	float4 color : COLOR;
};

//cbuffer ConstantBuffer : register(b0)
//{
//	float4x4 wvpMat;
//};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT output;
	//output.pos = mul(input.pos, wvpMat);
	output.pos = input.pos;
	output.texCoord = input.texCoord;
	output.color = input.color;
	return output;
}

