#pragma once

#ifndef _CAMERA_H_
#define _CAMERA_H_

#include "input.h"
#include "coldet.h"
#include <d3d12.h>


class gCamera;

class gViewingFrustum
{
public:
	gViewingFrustum( gCamera* cam );

	bool testPoint( float x, float y, float z )  const;
	bool testPoint( const DirectX::XMFLOAT3& point )  const;
	bool testAABB( const DirectX::XMFLOAT3& bbMin, const DirectX::XMFLOAT3& bbMax )  const;
	bool testAABB( const gAABB& bbox )  const;

	void updatePlanes();

protected:

	gViewingFrustum();

	union
	{
		struct {
			DirectX::XMVECTOR m_nearPlane, m_farPlane, m_leftPlane, m_rightPlane, m_topPlane, m_bottomPlane;
		};
		struct {
			DirectX::XMVECTOR m_planes[6];
		};
	};

	gCamera* m_pCam;
};

class gCamera
{
public:
	gCamera( gInput* input );
	gCamera();
	~gCamera();

	void tick(float dt);

	const DirectX::XMMATRIX& getViewMatrix() const;
	const DirectX::XMMATRIX& getProjMatrix() const;
	const DirectX::XMMATRIX& getViewProjMatrix() const;

	const DirectX::XMVECTOR& getPosition( ) const;
	const DirectX::XMVECTOR& getOrientation( ) const;
	float getAspectRatio() const;
	float getFOV() const;
	float getFarPlane() const;
	float getNearPlane() const;

	//test
	float getYaw() const;
	float getPitch() const;

	const gViewingFrustum& getViewingFrustum() const;

	void setPosition( const DirectX::XMFLOAT3& vec );
	void setOrientation( const DirectX::XMFLOAT4& q );
	void setOrientation(const DirectX::XMFLOAT3& dir);
	void setAspectRatio( float aspectRatio );
	void setFOV( float FOV );
	void setFarPlane( float farPlane );
	void setNearPlane( float nearPlane );


	void lookAt( const DirectX::XMFLOAT3& target );
	void lookAt( const DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& newCamPosition );

	void setMovementSpeed( float speed );
	void setRotationSpeed( float speed );
	void setDefaults();
	void setInput( gInput* input );

	void projPointToScreen( const DirectX::XMFLOAT3& point, 
		DirectX::XMFLOAT3& outPoint, const D3D12_VIEWPORT& viewport ) const;

	float getDistanceToPointF(const DirectX::XMFLOAT3& vec) const;
	unsigned short getDistanceToPointUS(const DirectX::XMFLOAT3& vec) const;

protected:
	
	void recompMatrices();
	void _YPtoQuat();

	gInput* m_input;
	DirectX::XMVECTOR m_rot;
	float m_yaw; 
	float m_pitch;
	DirectX::XMVECTOR m_pos;
	float m_tspeed, m_rspeed, m_aspect, m_FOV, m_fPlane, m_nPlane;
	gViewingFrustum m_frustum;
	DirectX::XMMATRIX m_mview;
	DirectX::XMMATRIX m_mproj;
	DirectX::XMMATRIX m_mviewproj;
};


#endif
