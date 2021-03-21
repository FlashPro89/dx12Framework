#pragma once

#ifndef _DX12_MESH_GENERATOR_H_
#define _DX12_MESH_GENERATOR_H_

#include "RingUploadBuffer.h"

struct MeshVertex
{
	float x, y, z; // pos
	float nx, ny, nz; // normal
	float tx, ty, tz; // tangent
	float tu, tv; // texcoord
};

struct DX12MeshGeneratorOffets
{
	ADDRESS vDataOffset;
	ADDRESS iDataOffset;
	UINT vDataSize;
	UINT iDataSize;
	UINT vertexesNum;
	UINT indexesNum;
	UINT primitiveCount;
	void* vdata;
	void* idata;
};

constexpr UINT CUBEVERTSNUM = 8;
constexpr UINT CUBEINDEXESNUM = 36;


template <INT posOffs, INT normOffs, INT tangOffs, INT tcOffs> class DX12MeshGenerator
{
public:
	union _v3
	{
		struct {

			float x, y, z;
		};
		float v[3];
	};
	union _v2
	{
		struct {

			float x, y;
		};
		float v[2];
	};

	DX12MeshGenerator(std::shared_ptr<gRingUploadBuffer> buffer, UINT alignment =
			D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) :
		m_spRingBuffer(buffer), m_alignment(alignment) {}

	bool buildCube( DX12MeshGeneratorOffets& outOffs, float w, float h, float d, float tscale = 1.f)
	{
		return false;

	}
	bool buildIndexedCube( DX12MeshGeneratorOffets& outOffs, float w, float h, float d, bool use32bitIndexes = false, float tscale = 1.f, float toffset = 0.f )
	{
		UINT64 availableSize = m_spRingBuffer->getAvailableAllocationSize(m_alignment);

		UINT iStride = 0, vStride = 0;

		if (posOffs >= 0) vStride += sizeof(_v3);
		if (normOffs >= 0) vStride += sizeof(_v3);
		if (tangOffs >= 0) vStride += sizeof(_v3);
		if (tcOffs >= 0) vStride += sizeof(_v2);

		iStride = use32bitIndexes ? sizeof(DWORD) : sizeof(WORD);

		UINT vSizeNeeded = 24 * vStride;
		UINT iSizeNeeded = 36 * iStride;

		outOffs.vDataSize = vSizeNeeded;
		outOffs.iDataSize = iSizeNeeded;

		if (availableSize < (vSizeNeeded + iSizeNeeded + m_alignment * 2)) // проверяем с запасом
			return false;

		outOffs.vdata = m_spRingBuffer->allocate(0, vSizeNeeded, m_alignment, &outOffs.vDataOffset);
		outOffs.idata = m_spRingBuffer->allocate(0, iSizeNeeded, m_alignment, &outOffs.iDataOffset);

		unsigned char* vdata = reinterpret_cast<unsigned char*>(outOffs.vdata);
		unsigned char* idata = reinterpret_cast<unsigned char* >(outOffs.idata);

		_v3* v3 = 0; _v2* v2 = 0;

		unsigned char* vdataCursour = 0;
		unsigned char* idataCursour = 0;

//------------------------------------------------------------
		if (posOffs >= 0)
		{
			_v3 pos[24];

			//front
			pos[0].x = -w; pos[0].y = -h; pos[0].z = -d;
			pos[1].x = -w; pos[1].y = h; pos[1].z = -d;
			pos[2].x = w; pos[2].y = h; pos[2].z = -d;
			pos[3].x = w; pos[3].y = -h; pos[3].z = -d;

			//back
			pos[4].x = w; pos[4].y = -h; pos[4].z = d;
			pos[5].x = w; pos[5].y = h; pos[5].z = d;
			pos[6].x = -w; pos[6].y = h; pos[6].z = d;
			pos[7].x = -w; pos[7].y = -h; pos[7].z = d;

			//left
			pos[8].x = -w; pos[8].y = -h; pos[8].z = d;
			pos[9].x = -w; pos[9].y = h; pos[9].z = d;
			pos[10].x = -w; pos[10].y = h; pos[10].z = -d;
			pos[11].x = -w; pos[11].y = -h; pos[11].z = -d;

			//right
			pos[12].x = w; pos[12].y = -h; pos[12].z = -d;
			pos[13].x = w; pos[13].y = h; pos[13].z = -d;
			pos[14].x = w; pos[14].y = h; pos[14].z = d;
			pos[15].x = w; pos[15].y = -h; pos[15].z = d;

			//top
			pos[16].x = -w; pos[16].y = h; pos[16].z = -d;
			pos[17].x = -w; pos[17].y = h; pos[17].z = d;
			pos[18].x = w; pos[18].y = h; pos[18].z = d;
			pos[19].x = w; pos[19].y = h; pos[19].z = -d;

			//down
			pos[20].x = -w; pos[20].y = -h; pos[20].z = d;
			pos[21].x = -w; pos[21].y = -h; pos[21].z = -d;
			pos[22].x = w; pos[22].y = -h; pos[22].z = -d;
			pos[23].x = w; pos[23].y = -h; pos[23].z = d;


			vdataCursour = vdata;
			for (int i = 0; i < 24; i++)
			{
				v3 = reinterpret_cast<_v3*>(vdataCursour + posOffs);
				*v3 = pos[i];
				vdataCursour += vStride;
			}
		}

//------------------------------------------------------------
		if (normOffs >= 0)
		{
			_v3 norm[24];

			_v3 left = { -1.f,0,0 }; _v3 right = { 1.f,0,0 };
			_v3 top = { 0,1.f,0 }; _v3 down = { 0,-1.f,0 };
			_v3 front = { 0,0,-1.f }; _v3 back = { 0,0,1.f };

			norm[0] = front; norm[1] = front; norm[2] = front; norm[3] = front;
			norm[4] = back; norm[5] = back; norm[6] = back; norm[7] = back;
			norm[8] = left; norm[9] = left; norm[10] = left; norm[11] = left;
			norm[12] = right; norm[13] = right; norm[14] = right; norm[15] = right;
			norm[16] = top; norm[17] = top; norm[18] = top; norm[19] = top;
			norm[20] = down; norm[21] = down; norm[22] = down; norm[23] = down;

			vdataCursour = vdata;
			for (int i = 0; i < 24; i++)
			{
				v3 = reinterpret_cast<_v3*>(vdataCursour + normOffs);
				*v3 = norm[i];
				vdataCursour += vStride;
			}
		}

//------------------------------------------------------------
		if (tangOffs >= 0)
		{
			_v3 tang[24];

			_v3 left = { 0,0,-1.f }; _v3 right = { 0,0,1.f };
			_v3 top = { 1.f,0,0 }; _v3 down = { 1.f,0,0 };
			_v3 front = { 1.f,0,0 }; _v3 back = { 1.f,0,0 };

			tang[0] = front; tang[1] = front; tang[2] = front; tang[3] = front;
			tang[4] = back; tang[5] = back; tang[6] = back; tang[7] = back;
			tang[8] = left; tang[9] = left; tang[10] = left; tang[11] = left;
			tang[12] = right; tang[13] = right; tang[14] = right; tang[15] = right;
			tang[16] = top; tang[17] = top; tang[18] = top; tang[19] = top;
			tang[20] = down; tang[21] = down; tang[22] = down; tang[23] = down;

			vdataCursour = vdata;
			for (int i = 0; i < 24; i++)
			{
				v3 = reinterpret_cast<_v3*>(vdataCursour + tangOffs);
				*v3 = tang[i];
				vdataCursour += vStride;
			}

		}
//------------------------------------------------------------
		if (tcOffs >= 0)
		{
			_v2 tc[24];

			for (int i = 0; i < 24; i += 4)
			{
				tc[i].x = toffset;		tc[i].y = tscale;
				tc[i+1].x = toffset;		tc[i+1].y = toffset;
				tc[i + 2].x = tscale;	tc[i + 2].y = toffset;
				tc[i + 3].x = tscale;	tc[i + 3].y = tscale;
			}

			vdataCursour = vdata;
			for (int i = 0; i < 24; i++)
			{
				v2 = reinterpret_cast<_v2*>(vdataCursour + tcOffs);
				*v2 = tc[i];
				vdataCursour += vStride;
			}
		}

		//generate indexes:
		if (use32bitIndexes)
		{
			DWORD* iDataCursour = reinterpret_cast<DWORD*>(idata);
			for (int i = 0; i < 36; i += 6)
			{
				*iDataCursour++ = i;
				*iDataCursour++ = i+1;
				*iDataCursour++ = i+2;
				*iDataCursour++ = i;
				*iDataCursour++ = i+2;
				*iDataCursour++ = i+3;
			}
		}
		else
		{
			WORD* iDataCursour = reinterpret_cast<WORD*>(idata);
			for (int i = 0; i < 36; i += 4)
			{
				*iDataCursour++ = i;
				*iDataCursour++ = i + 1;
				*iDataCursour++ = i + 2;
				*iDataCursour++ = i;
				*iDataCursour++ = i + 2;
				*iDataCursour++ = i + 3;
			}
		}

		outOffs.vertexesNum = 24;
		outOffs.indexesNum = 36;
		outOffs.primitiveCount = outOffs.indexesNum / 3;
		return true;
	}

protected:
	std::shared_ptr<gRingUploadBuffer> m_spRingBuffer;
	UINT m_alignment;
};

#endif

