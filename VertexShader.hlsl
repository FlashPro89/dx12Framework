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

struct sConstantBuffer
{
	//float4x4 wvpMat;
	float2 pos;
	float2 rot;
};

ConstantBuffer<sConstantBuffer> myCBuffer : register(b0);

VS_OUTPUT main(VS_INPUT input)
{

	VS_OUTPUT output;
	//output.pos = mul(input.pos, wvpMat);
	output.pos= input.pos;

	//output.pos.x = cos(myCBuffer.rot.x) * sqrt(input.pos.x* input.pos.x + input.pos.y* input.pos.y );
	//output.pos.y = sin(myCBuffer.rot.x) * sqrt(input.pos.x * input.pos.x + input.pos.y * input.pos.y);

	output.pos.xy += myCBuffer.pos.xy;
	output.pos.xy *= myCBuffer.rot.xy;

	output.texCoord = input.texCoord;
	output.color = input.color;
	return output;
}

