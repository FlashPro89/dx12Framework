#pragma once

#ifndef _COL_DET_H_
#define _COL_DET_H_

#include <d3dtypes.h>
#include <DirectXMath.h>

class gAABB
{

public:
	gAABB();
	gAABB( const DirectX::XMFLOAT3& min, const DirectX::XMFLOAT3& max);

	const DirectX::XMFLOAT3& getMaxBounds() const;
	const DirectX::XMFLOAT3& getMinBounds() const;

	void setMaxBounds( const DirectX::XMFLOAT3& max );
	void setMinBounds( const DirectX::XMFLOAT3& min );
	void setMaxBounds( float x, float y, float z );
	void setMinBounds( float x, float y, float z );

	void addPoint( const DirectX::XMFLOAT3& point );
	void addAABB( const gAABB& other );

	void reset();
	bool isEmpty() const;

	void getTransformedByMatrix( gAABB& out, const DirectX::XMMATRIX& transform ) const;

	void getCenterPoint(DirectX::XMFLOAT3* outCenter);
	void setScale( float scale );

protected:
	DirectX::XMFLOAT3 m_bmin;
	DirectX::XMFLOAT3 m_bmax;
};

#endif