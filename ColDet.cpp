#include "ColDet.h"

#define MAX_WORLD_SIZE 128000.f

gAABB::gAABB() 
{
	reset();
}

gAABB::gAABB( const DirectX::XMFLOAT3& min, const DirectX::XMFLOAT3& max)
{
	m_bmin = min; m_bmax = max;
}

const DirectX::XMFLOAT3& gAABB::getMaxBounds() const
{
	return m_bmax;
}

const DirectX::XMFLOAT3& gAABB::getMinBounds() const
{
	return m_bmin;
}

void gAABB::setMaxBounds( const DirectX::XMFLOAT3& max)
{
	m_bmax = max;
}

void gAABB::setMinBounds( const DirectX::XMFLOAT3& min)
{
	m_bmin = min;
}

void gAABB::setMaxBounds(float x, float y, float z)
{
	m_bmax.x = x;
	m_bmax.y = y;
	m_bmax.z = z;
}

void gAABB::setMinBounds(float x, float y, float z)
{
	m_bmin.x = x;
	m_bmin.y = y;
	m_bmin.z = z;
}

void gAABB::addPoint( const DirectX::XMFLOAT3& point )
{
	m_bmin.x = point.x < m_bmin.x ? point.x : m_bmin.x;
	m_bmin.y = point.y < m_bmin.y ? point.y : m_bmin.y;
	m_bmin.z = point.z < m_bmin.z ? point.z : m_bmin.z;

	m_bmax.x = point.x > m_bmax.x ? point.x : m_bmax.x;
	m_bmax.y = point.y > m_bmax.y ? point.y : m_bmax.y;
	m_bmax.z = point.z > m_bmax.z ? point.z : m_bmax.z;
}

void gAABB::addAABB( const gAABB& other ) //optimize after debug
{
	if (other.isEmpty())
		return;

	DirectX::XMFLOAT3 min = other.getMinBounds();
	DirectX::XMFLOAT3 max = other.getMaxBounds();

	addPoint(min);
	addPoint(max);
}

void gAABB::reset()
{
	setMinBounds( MAX_WORLD_SIZE, MAX_WORLD_SIZE, MAX_WORLD_SIZE );
	setMaxBounds( -MAX_WORLD_SIZE, -MAX_WORLD_SIZE, -MAX_WORLD_SIZE );
}

bool gAABB::isEmpty() const
{
	return ( (m_bmin.x >= MAX_WORLD_SIZE) && (m_bmin.y >= MAX_WORLD_SIZE) && (m_bmin.z >= MAX_WORLD_SIZE) &&
			 (m_bmax.x <= -MAX_WORLD_SIZE) && (m_bmax.y <= -MAX_WORLD_SIZE) && (m_bmax.z <= -MAX_WORLD_SIZE) );
}

void gAABB::getTransformedByMatrix( gAABB& out, const DirectX::XMMATRIX& transform ) const
{
	DirectX::XMFLOAT3 points[8] =
	{
		DirectX::XMFLOAT3(m_bmin.x, m_bmin.y, m_bmin.z),
		DirectX::XMFLOAT3(m_bmax.x, m_bmin.y, m_bmin.z),
		DirectX::XMFLOAT3(m_bmin.x, m_bmax.y, m_bmin.z),
		DirectX::XMFLOAT3(m_bmax.x, m_bmax.y, m_bmin.z),

		DirectX::XMFLOAT3(m_bmin.x, m_bmin.y, m_bmax.z),
		DirectX::XMFLOAT3(m_bmax.x, m_bmin.y, m_bmax.z),
		DirectX::XMFLOAT3(m_bmin.x, m_bmax.y, m_bmax.z),
		DirectX::XMFLOAT3(m_bmax.x, m_bmax.y, m_bmax.z),
	};

	DirectX::XMFLOAT3 out_points[8];

	//D3DXVec3TransformCoordArray((DirectX::XMFLOAT3*)&out_points[0], sizeof(DirectX::XMFLOAT3), 
		//(const DirectX::XMFLOAT3*)&points[0], sizeof(DirectX::XMFLOAT3), &transform, 8);

	DirectX::XMVector3TransformCoordStream(out_points, sizeof(DirectX::XMFLOAT3), points, sizeof(DirectX::XMFLOAT3), 8, transform);

	out.reset();

	for( int i = 0; i<8; i++ )
		out.addPoint(out_points[i]);
}

void gAABB::getCenterPoint( DirectX::XMFLOAT3* outCenter )
{
	outCenter->x = (m_bmax.x - m_bmin.x) * 0.5f + m_bmin.x;
	outCenter->y = (m_bmax.y - m_bmin.y) * 0.5f + m_bmin.y;
	outCenter->z = (m_bmax.z - m_bmin.z) * 0.5f + m_bmin.z;
}

void gAABB::setScale( float scale ) // TODO: функция дает неправильный результат!
{
	//DirectX::XMFLOAT3 scaledBound = ( m_bmax - m_bmin ) * ( scale - 1 ); // TODO ??:
	//m_bmax += scaledBound;
	//m_bmin -= scaledBound;
	//m_bmax *= scale;
	//m_bmin *= scale;
}
