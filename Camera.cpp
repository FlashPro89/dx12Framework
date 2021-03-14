#include "Camera.h"
#include "gmath.h"

#define DEFAULT_CAM_TSPEED 1200.f
#define DEFAULT_CAM_RSPEED 0.9f
#define DEFAULT_CAM_FOV    XM_PIDIV4 //D3DX_PI / 4.0f
#define DEFAULT_CAM_FPLANE 15000.f
#define DEFAULT_CAM_NPLANE FLT_EPSILON
#define DEFAULT_CAM_ASPECT 1.333f
#define MOUSE_MAX_MOVEMENT 15

using namespace DirectX;

gCamera::gCamera( gInput* input ) : m_frustum(this)
{
	m_input = input;
	setDefaults();
	XMFLOAT3 f3p(0, 0, 0);
	m_pos = XMLoadFloat3( &f3p );
	m_rot = XMQuaternionIdentity();
	recompMatrices();
}

gCamera::gCamera() : m_frustum(this)
{
	m_input = 0;
	setDefaults();
	XMFLOAT3 f3p(0, 0, 0);
	m_pos = XMLoadFloat3(&f3p);
	m_rot = XMQuaternionIdentity();
	recompMatrices();
}

gCamera::~gCamera()
{

}

const XMMATRIX& gCamera::getViewMatrix()  const
{
	return m_mview;
}
const XMMATRIX& gCamera::getProjMatrix()  const
{
	return m_mproj;
}

const XMMATRIX& gCamera::getViewProjMatrix()  const
{
	return m_mviewproj;
}

const gViewingFrustum& gCamera::getViewingFrustum()  const
{
	return m_frustum;
}

void gCamera::tick(float dt)
{
	bool changed = false;

	if (m_input)
	{
		
		if (m_input->isMousePressed(0))
		{
			changed = true;

			int x = m_input->getMouseX();
			int y = m_input->getMouseY();

			if (abs(x) > MOUSE_MAX_MOVEMENT)
			{
				if (x < 0)
					x = MOUSE_MAX_MOVEMENT;
				else
					x = -MOUSE_MAX_MOVEMENT;
			}

			if (abs(y) > MOUSE_MAX_MOVEMENT)
			{
				if (y < 0)
					y = MOUSE_MAX_MOVEMENT;
				else
					y = -MOUSE_MAX_MOVEMENT;
			}

			m_yaw += x * dt * m_rspeed;

			m_pitch += y * dt * m_rspeed;
			if (m_pitch > XM_PI / 2.5f)
				m_pitch = XM_PI / 2.5f;
			if (m_pitch < -XM_PI / 2.5f)
				m_pitch = -XM_PI / 2.5f;

			_YPtoQuat();
		}

		XMMATRIX mat_dir;
		//D3DXRotationQuaternion(&mat_dir, &m_rot);
		mat_dir = XMMatrixRotationQuaternion(m_rot);
		//XMMatrixTranspose(mat_dir);

		XMFLOAT3 f3_dir_forward (0, 0, 1);
		XMFLOAT3 f3_dir_left(-1, 0, 0);
		XMFLOAT3 f3_dir_up(0, 1, 0);

		XMVECTOR dir_forward = XMLoadFloat3( &f3_dir_forward );
		XMVECTOR dir_left = XMLoadFloat3(&f3_dir_left);
		XMVECTOR dir_up = XMLoadFloat3(&f3_dir_up);

		//D3DXVec3TransformCoord(&dir_forward, &dir_forward, &mat_dir);
		dir_forward = XMVector3TransformCoord(dir_forward, mat_dir);

		//D3DXVec3Normalize(&dir_forward, &dir_forward);
		XMVector3Normalize(dir_forward);

		//D3DXVec3Cross(&dir_left, &dir_forward, &dir_up);
		dir_left = XMVector3Cross(dir_forward, dir_up);

		//D3DXVec3Normalize(&dir_left, &dir_left);
		XMVector3Normalize(dir_left);
		
		if (m_input->isKeyPressed(DIK_W))
		{
			m_pos += dir_forward * dt * m_tspeed;
			changed = true;
		}
		if (m_input->isKeyPressed(DIK_S))
		{
			m_pos -= dir_forward * dt * m_tspeed;
			changed = true;
		}
		if (m_input->isKeyPressed(DIK_A))
		{
			m_pos += dir_left * dt * m_tspeed;
			changed = true;
		}
		if (m_input->isKeyPressed(DIK_D))
		{
			m_pos -= dir_left * dt * m_tspeed;
			changed = true;
		}

	}

	if (changed)
	{
		recompMatrices();
	}
}

void gCamera::setMovementSpeed(float speed)
{
	m_tspeed = speed;
}
void gCamera::setRotationSpeed(float speed)
{
	m_rspeed = speed;
}

void gCamera::setDefaults()
{
	m_rspeed = DEFAULT_CAM_RSPEED;
	m_tspeed = DEFAULT_CAM_TSPEED;
	m_FOV = DEFAULT_CAM_FOV;
	m_fPlane = DEFAULT_CAM_FPLANE;
	m_nPlane = DEFAULT_CAM_NPLANE;
	m_aspect = DEFAULT_CAM_ASPECT;
	m_yaw = 0;
	m_pitch = 0;

	m_mview = m_mproj = m_mviewproj = XMMatrixIdentity();
}

void gCamera::setInput(gInput* input)
{
	m_input = input;
}

void gCamera::projPointToScreen(const XMFLOAT3& point, XMFLOAT3& outPoint, const D3D12_VIEWPORT& vp ) const
{
	//D3DXVec3Transform( &outPoint, &point, &m_mviewproj );
	XMMATRIX mId = XMMatrixIdentity();
	XMVECTOR v = XMLoadFloat3(&point);
	
	//D3DXVec3Project(&outPoint, &point, &viewport, &m_mviewproj, &mId, &mId);
	XMVector3Project(v, vp.TopLeftX, vp.TopLeftY, vp.Width, vp.Height, 
		vp.MinDepth, vp.MaxDepth, this->m_mviewproj, mId, mId);

	XMVECTOR in, out;
	in = XMLoadFloat3(&point);
	//D3DXVec3Transform(&out, &point, &m_mviewproj);
	out = XMVector4Transform( in, m_mviewproj );

	XMFLOAT4 fout; 
	XMStoreFloat4( &fout, out );

	fout.x = fout.x / fout.w;
	fout.y = fout.y / fout.w;

	DWORD halfW = (DWORD)(( vp.Width - vp.TopLeftX ) / 2);
	DWORD halfH = (DWORD)(( vp.Height - vp.TopLeftY ) / 2);

	fout.x *= halfW; 
	fout.y *= halfH;

	fout.x += halfW;
	fout.y = halfH - fout.y;

	fout.x += vp.TopLeftX;
	fout.y += vp.TopLeftY;

	fout.z = fout.z / fout.w - 1.f;

	outPoint = XMFLOAT3( fout.x, fout.y, fout.z );
}

const XMVECTOR& gCamera::getPosition() const
{
	return m_pos;
}

const XMVECTOR& gCamera::getOrientation()  const
{
	return m_rot;
}

float gCamera::getAspectRatio() const
{
	return m_aspect;
}

float gCamera::getFOV() const
{
	return m_FOV;
}


float gCamera::getYaw() const
{
	return m_yaw;
}

float gCamera::getPitch() const
{
	return m_pitch;
}

void gCamera::setPosition(const XMFLOAT3& vec)
{
	m_pos = XMLoadFloat3(&vec);
	recompMatrices();
}

void gCamera::setOrientation( const XMFLOAT4& q )
{
	m_rot = XMLoadFloat4(&q);
	recompMatrices();
}

void gCamera::setOrientation(const XMFLOAT3& dir)
{
	XMVECTOR q;
	XMVECTOR vdir = XMLoadFloat3( &dir );
	XMFLOAT3 fup = XMFLOAT3(0.f, 0.f, 1.f);
	XMVECTOR vup = XMLoadFloat3( &fup );
	float yaw, pitch, roll;
	_quatShortestArc(q, vup, vdir);
	_quatToYawPitchRoll(q, &yaw, &pitch, &roll );

	m_yaw = yaw;
	m_pitch = pitch;
	_YPtoQuat();

	recompMatrices();
}

void gCamera::setAspectRatio(float aspectRatio)
{
	m_aspect = aspectRatio;

	recompMatrices();
}

void gCamera::setFOV(float FOV)
{
	m_FOV = FOV;
	recompMatrices();
}

void gCamera::lookAt( const XMFLOAT3& target )
{
	XMVECTOR vtarget = XMLoadFloat3( &target );
	XMVECTOR dir = vtarget - m_pos;
	//D3DXVec3Normalize( &dir, &dir );
	dir = XMVector3Normalize(dir);

	XMVECTOR q;
	XMFLOAT3 fup = XMFLOAT3(0, 0, 1.f);
	XMVECTOR vup = XMLoadFloat3(&fup);
	float yaw, pitch, roll;
	_quatShortestArc( q, vup, dir );
	_quatToYawPitchRoll( q, &yaw, &pitch, &roll );

	_YawPitchRolltoQuat(q, yaw, pitch, roll);

	m_pitch = pitch;
	m_yaw = yaw;

	_YPtoQuat();
	recompMatrices();
}

void gCamera::lookAt( const XMFLOAT3& target, const XMFLOAT3& newCamPosition )
{
	m_pos = XMLoadFloat3( &newCamPosition );
	lookAt( target );
}

void gCamera::_YPtoQuat()
{
	//D3DXQuaternionRotationYawPitchRoll( &m_rot, m_yaw, m_pitch, 0.f);
	m_rot = XMQuaternionRotationRollPitchYaw( m_pitch, m_yaw, 0.f );
}

void gCamera::recompMatrices()
{
	XMMATRIX rot;
	XMFLOAT3 fdir(0, 0, 1.f);
	XMFLOAT3 fup = XMFLOAT3(0, 1.0f, 0);
	XMVECTOR up = XMLoadFloat3(&fup);
	XMVECTOR dir = XMLoadFloat3(&fdir);

	//XMMATRIXRotationQuaternion(&rot, &m_rot);
	rot = XMMatrixRotationQuaternion( m_rot );

	//D3DXVec3TransformCoord(&dir, &dir, &rot);
	dir = XMVector3Transform( dir, rot );
	dir += m_pos;
	//dir = XMVector3Normalize(dir);
	//XMMATRIXLookAtLH( &m_mview, &m_pos, &dir, &XMFLOAT3(0, 1.0f, 0) );
	m_mview = XMMatrixLookAtLH(m_pos, dir, up);

	//XMMATRIXPerspectiveFovLH( &m_mproj, m_FOV, m_aspect, m_nPlane, m_fPlane );
	m_mproj = XMMatrixPerspectiveFovLH(m_FOV, m_aspect, m_nPlane, m_fPlane);

	//XMMATRIXMultiply( &m_mviewproj, &m_mview, &m_mproj );
	m_mviewproj = XMMatrixMultiply(m_mview, m_mproj);

	//перестраиваем пирамиду видимости
	m_frustum.updatePlanes();
}


gViewingFrustum::gViewingFrustum( gCamera* cam )
{
	if (!cam)
		throw("Error: null gcamera pointer in frustum");
	m_pCam = cam;
}

bool gViewingFrustum::testPoint( float x, float y, float z )  const
{
	return testPoint( XMFLOAT3(x, y, z) );
}

bool gViewingFrustum::testPoint(const XMFLOAT3& point)  const
{
	float dot = 0;
	XMVECTOR vdot;
	XMVECTOR vp = XMLoadFloat3( &point );

	// ”беждаемс€, что точка внутри пирамиды
	for (short i = 0; i < 6; i++)
	{
		//dot = D3DXPlaneDotCoord(&m_planes[i], &XMFLOAT3(x, y, z));
		vdot = XMPlaneDotCoord(m_planes[i], vp);
		XMStoreFloat(&dot, vdot);
		if (dot < 0.0f)
			return false;
	}
	return true;
}
bool gViewingFrustum::testAABB(const XMFLOAT3& bbMin, const XMFLOAT3& bbMax)  const
{
	short Count;
	bool PointIn;
	XMVECTOR vd, vt;
	XMFLOAT3 fp;
	float dst;

	// ѕодсчитываем количество точек внутри пирамиды
	for (short i = 0; i < 6; i++)
	{
		Count = 8;
		PointIn = true;

		// ѕровер€ем все восемь точек относительно плоскости
		fp = XMFLOAT3(bbMin.x, bbMin.y, bbMin.z);
		vt = XMLoadFloat3( &fp );
		vd = XMPlaneDotCoord( m_planes[i], vt );
		XMLoadFloat(&dst);
		if (dst < 0.0f) {
			PointIn = false;
			Count--;
		}

		fp = XMFLOAT3(bbMax.x, bbMin.y, bbMin.z);
		vt = XMLoadFloat3(&fp);
		vd = XMPlaneDotCoord(m_planes[i], vt);
		XMLoadFloat(&dst);
		if (dst < 0.0f) {
			PointIn = false;
			Count--;
		}

		fp = XMFLOAT3(bbMin.x, bbMax.y, bbMin.z);
		vt = XMLoadFloat3(&fp);
		vd = XMPlaneDotCoord(m_planes[i], vt);
		XMLoadFloat(&dst);
		if (dst < 0.0f) {
			PointIn = false;
			Count--;
		}

		fp = XMFLOAT3(bbMax.x, bbMax.y, bbMin.z);
		vt = XMLoadFloat3(&fp);
		vd = XMPlaneDotCoord(m_planes[i], vt);
		XMLoadFloat(&dst);
		if (dst < 0.0f) {
			PointIn = false;
			Count--;
		}

		fp = XMFLOAT3(bbMin.x, bbMin.y, bbMax.z);
		vt = XMLoadFloat3(&fp);
		vd = XMPlaneDotCoord(m_planes[i], vt);
		XMLoadFloat(&dst);
		if (dst < 0.0f) {
			PointIn = false;
			Count--;
		}

		fp = XMFLOAT3(bbMax.x, bbMin.y, bbMax.z);
		vt = XMLoadFloat3(&fp);
		vd = XMPlaneDotCoord(m_planes[i], vt);
		XMLoadFloat(&dst);
		if (dst < 0.0f) {
			PointIn = false;
			Count--;
		}

		fp = XMFLOAT3(bbMin.x, bbMax.y, bbMax.z);
		vt = XMLoadFloat3(&fp);
		vd = XMPlaneDotCoord(m_planes[i], vt);
		XMLoadFloat(&dst);
		if (dst < 0.0f) {
			PointIn = false;
			Count--;
		}

		fp = XMFLOAT3(bbMax.x, bbMax.y, bbMax.z);
		vt = XMLoadFloat3(&fp);
		vd = XMPlaneDotCoord(m_planes[i], vt);
		XMLoadFloat(&dst);
		if (dst < 0.0f) {
			PointIn = false;
			Count--;
		}

		// ≈сли внутри пирамиды ничего нет, возвращаем FALSE
		if (Count == 0)
			return false;
	}
	return true;
}

bool gViewingFrustum::testAABB( const gAABB& bbox ) const
{
	return testAABB( bbox.getMaxBounds(), bbox.getMinBounds() );
}
	
void gViewingFrustum::updatePlanes()
{
	const XMMATRIX& m = m_pCam->getViewProjMatrix();
	XMFLOAT4X4 Matrix;
	XMStoreFloat4x4( &Matrix, m );
	
	XMFLOAT4 planes[6];
	for( int i = 0; i<6; i++ )
		XMStoreFloat4(&planes[i], m_planes[i]);

	//// ¬ычисл€ем плоскости
	planes[0].x = Matrix._14 + Matrix._13;
	planes[0].y = Matrix._24 + Matrix._23;
	planes[0].z = Matrix._34 + Matrix._33;
	planes[0].w = Matrix._44 + Matrix._43;
	//D3DXPlaneNormalize(&m_nearPlane, &m_nearPlane);
	m_planes[0] = XMLoadFloat4(&planes[0]);
	XMPlaneNormalize(m_planes[0]);

	planes[1].x = Matrix._14 - Matrix._13;
	planes[1].y = Matrix._24 - Matrix._23;
	planes[1].z = Matrix._34 - Matrix._33;
	planes[1].w = Matrix._44 - Matrix._43;
	//D3DXPlaneNormalize(&m_farPlane, &m_farPlane);
	m_planes[1] = XMLoadFloat4(&planes[1]);
	XMPlaneNormalize(m_planes[1]);

	planes[2].x = Matrix._14 + Matrix._11;
	planes[2].y = Matrix._24 + Matrix._21;
	planes[2].z = Matrix._34 + Matrix._31;
	planes[2].w = Matrix._44 + Matrix._41;
	//D3DXPlaneNormalize(&m_leftPlane, &m_leftPlane);
	m_planes[2] = XMLoadFloat4(&planes[2]);
	XMPlaneNormalize(m_planes[2]);

	planes[3].x = Matrix._14 - Matrix._11;
	planes[3].y = Matrix._24 - Matrix._21;
	planes[3].z = Matrix._34 - Matrix._31;
	planes[3].w = Matrix._44 - Matrix._41;
	//D3DXPlaneNormalize(&m_rightPlane, &m_rightPlane);
	m_planes[3] = XMLoadFloat4(&planes[3]);
	XMPlaneNormalize(m_planes[3]);

	planes[4].x = Matrix._14 - Matrix._12;
	planes[4].y = Matrix._24 - Matrix._22;
	planes[4].z = Matrix._34 - Matrix._32;
	planes[4].w = Matrix._44 - Matrix._42;
	//D3DXPlaneNormalize(&m_topPlane, &m_topPlane);
	m_planes[4] = XMLoadFloat4(&planes[4]);
	XMPlaneNormalize(m_planes[4]);

	planes[5].x = Matrix._14 + Matrix._12;
	planes[5].y = Matrix._24 + Matrix._22;
	planes[5].z = Matrix._34 + Matrix._32;
	planes[5].w = Matrix._44 + Matrix._42;
	//D3DXPlaneNormalize(&m_bottomPlane, &m_bottomPlane);
	m_planes[5] = XMLoadFloat4(&planes[5]);
	XMPlaneNormalize(m_planes[5]);

	for (int i = 0; i < 6; i++)
		m_planes[i] = XMLoadFloat4( &planes[i] );
}

float gCamera::getDistanceToPointF( const XMFLOAT3& v ) const
{
	XMVECTOR vec;
	vec = XMLoadFloat3( &v );

	XMVECTOR vDist = m_pos - vec;
	float dst;
	//return D3DXVec3Length(&vDist);
	XMVector3Length(vDist);
	XMStoreFloat(&dst, vDist);
	return dst;
}

unsigned short gCamera::getDistanceToPointUS( const XMFLOAT3& vec ) const
{
	float dist = getDistanceToPointF(vec);
	if (dist >= m_fPlane)
		return 0xFFFF;
	else
		return unsigned short((dist / m_fPlane) * 0xFFFF);
}
