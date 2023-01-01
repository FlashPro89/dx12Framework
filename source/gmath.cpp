#include "gmath.h"

//#include <d3dx9.h>
#include <math.h>
#include "gmath.h"
#include <DirectXMath.h>
using namespace DirectX;

//===========================================================
//	FROM GPUGEMS IV:
//===========================================================

/**** EulerAngles.c - Convert Euler angles to/from matrix or quat ****/
/* Ken Shoemake, 1993 */

EulerAngles Eul_(float ai, float aj, float ah, int order)
{
	EulerAngles ea;
	ea.x = ai; ea.y = aj; ea.z = ah;
	ea.w = (float)order;
	return (ea);
}
/* Construct quaternion from Euler angles (in radians). */
Quat Eul_ToQuat(EulerAngles ea)
{
	Quat qu;
	float a[3], ti, tj, th, ci, cj, ch, si, sj, sh, cc, cs, sc, ss;
	int i, j, k, h, n, s, f;
	EulGetOrd(ea.w, i, j, k, h, n, s, f);
	if (f == EulFrmR) { float t = ea.x; ea.x = ea.z; ea.z = t; }
	if (n == EulParOdd) ea.y = -ea.y;
	ti = ea.x * 0.5f; tj = ea.y * 0.5f; th = ea.z * 0.5f;
	ci = cosf(ti);  cj = cosf(tj);  ch = cosf(th);
	si = sinf(ti);  sj = sinf(tj);  sh = sinf(th);
	cc = ci * ch; cs = ci * sh; sc = si * ch; ss = si * sh;
	if (s == EulRepYes) {
		a[i] = cj * (cs + sc);	/* Could speed up with */
		a[j] = sj * (cc + ss);	/* trig identities. */
		a[k] = sj * (cs - sc);
		qu.w = cj * (cc - ss);
	}
	else {
		a[i] = cj * sc - sj * cs;
		a[j] = cj * ss + sj * cc;
		a[k] = cj * cs - sj * sc;
		qu.w = cj * cc + sj * ss;
	}
	if (n == EulParOdd) a[j] = -a[j];
	qu.x = a[X]; qu.y = a[Y]; qu.z = a[Z];
	return (qu);
}

/* Construct matrix from Euler angles (in radians). */
void Eul_ToHMatrix(EulerAngles ea, HMatrix M)
{
	float ti, tj, th, ci, cj, ch, si, sj, sh, cc, cs, sc, ss;
	int i, j, k, h, n, s, f;
	EulGetOrd(ea.w, i, j, k, h, n, s, f);
	if (f == EulFrmR) { float t = ea.x; ea.x = ea.z; ea.z = t; }
	if (n == EulParOdd) { ea.x = -ea.x; ea.y = -ea.y; ea.z = -ea.z; }
	ti = ea.x;	  tj = ea.y;	th = ea.z;
	ci = cosf(ti); cj = cosf(tj); ch = cosf(th);
	si = sinf(ti); sj = sinf(tj); sh = sinf(th);
	cc = ci * ch; cs = ci * sh; sc = si * ch; ss = si * sh;
	if (s == EulRepYes) {
		M[i][i] = cj;	  M[i][j] = sj * si;    M[i][k] = sj * ci;
		M[j][i] = sj * sh;  M[j][j] = -cj * ss + cc; M[j][k] = -cj * cs - sc;
		M[k][i] = -sj * ch; M[k][j] = cj * sc + cs; M[k][k] = cj * cc - ss;
	}
	else {
		M[i][i] = cj * ch; M[i][j] = sj * sc - cs; M[i][k] = sj * cc + ss;
		M[j][i] = cj * sh; M[j][j] = sj * ss + cc; M[j][k] = sj * cs - sc;
		M[k][i] = -sj;	 M[k][j] = cj * si;    M[k][k] = cj * ci;
	}
	M[W][X] = M[W][Y] = M[W][Z] = M[X][W] = M[Y][W] = M[Z][W] = 0.f; M[W][W] = 1.f;
}

/* Convert matrix to Euler angles (in radians). */
EulerAngles Eul_FromHMatrix(HMatrix M, int order)
{
	EulerAngles ea;
	int i, j, k, h, n, s, f;
	EulGetOrd(order, i, j, k, h, n, s, f);
	if (s == EulRepYes) {
		float sy = sqrtf(M[i][j] * M[i][j] + M[i][k] * M[i][k]);
		if (sy > 16 * FLT_EPSILON) {
			ea.x = atan2f(M[i][j], M[i][k]);
			ea.y = atan2f(sy, M[i][i]);
			ea.z = atan2f(M[j][i], -M[k][i]);
		}
		else {
			ea.x = atan2f(-M[j][k], M[j][j]);
			ea.y = atan2f(sy, M[i][i]);
			ea.z = 0;
		}
	}
	else {
		float cy = sqrtf(M[i][i] * M[i][i] + M[j][i] * M[j][i]);
		if (cy > 16 * FLT_EPSILON) {
			ea.x = atan2f(M[k][j], M[k][k]);
			ea.y = atan2f(-M[k][i], cy);
			ea.z = atan2f(M[j][i], M[i][i]);
		}
		else {
			ea.x = atan2f(-M[j][k], M[j][j]);
			ea.y = atan2f(-M[k][i], cy);
			ea.z = 0;
		}
	}
	if (n == EulParOdd) { ea.x = -ea.x; ea.y = -ea.y; ea.z = -ea.z; }
	if (f == EulFrmR) { float t = ea.x; ea.x = ea.z; ea.z = t; }
	ea.w = (float)order;
	return (ea);
}

/* Convert quaternion to Euler angles (in radians). */
EulerAngles Eul_FromQuat(Quat q, int order)
{
	HMatrix M;
	float Nq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
	float s = (Nq > 0.f) ? (2.f / Nq) : 0.f;
	float xs = q.x * s, ys = q.y * s, zs = q.z * s;
	float wx = q.w * xs, wy = q.w * ys, wz = q.w * zs;
	float xx = q.x * xs, xy = q.x * ys, xz = q.x * zs;
	float yy = q.y * ys, yz = q.y * zs, zz = q.z * zs;
	M[X][X] = 1.f - (yy + zz); M[X][Y] = xy - wz; M[X][Z] = xz + wy;
	M[Y][X] = xy + wz; M[Y][Y] = 1.f - (xx + zz); M[Y][Z] = yz - wx;
	M[Z][X] = xz - wy; M[Z][Y] = yz + wx; M[Z][Z] = 1.f - (xx + yy);
	M[W][X] = M[W][Y] = M[W][Z] = M[X][W] = M[Y][W] = M[Z][W] = 0.f; M[W][W] = 1.f;
	return (Eul_FromHMatrix(M, order));
}
//===========================================================

void _quatShortestArc( XMVECTOR& out, const XMVECTOR& v0, const XMVECTOR& v1 )
{
	XMVECTOR cv = XMVector3Cross(v0, v1);
	//D3DXVec3Cross(&c, &v1, &v0);
	
	//float d = D3DXVec3Dot(&v0, &v1);
	XMVECTOR dv = XMVector3Dot(v0, v1);
	float d; 
	XMFLOAT3 n;

	XMStoreFloat( &d, dv );
	XMStoreFloat3( &n, cv );

	cv = XMVectorSet( n.x, n.y, n.z, d );
	XMQuaternionNormalize(cv);
	//out = D3DXQUATERNION(c.x, c.y, c.z, d);
	//D3DXQuaternionNormalize(&out, &out);

	XMFLOAT4 fout, fv0, fv1;
	XMStoreFloat4( &fout, cv );
	XMStoreFloat4( &fv0, v0 );
	XMStoreFloat4( &fv1, v1 );

	fout.w += 1.f; // reducing angle to halfangle

	if (fout.w <= 0.0000001f) // angle close to PI
	{
		if ((fv0.z * fv0.z) > (fv0.x * fv0.x))
			fout = XMFLOAT4(0, fv0.z, -fv0.y, fout.w); //from*vector3(1,0,0) 
		else
			fout = XMFLOAT4(fv0.y, -fv0.x, 0, fout.w); //from*vector3(0,0,1) 
	}
	out = XMVectorSet( fout.x, fout.y, fout.z, fout.w );
	
	XMQuaternionNormalize(out);
	//D3DXQuaternionNormalize(&out, &out);
}

void _quatToYawPitchRoll(const XMVECTOR& quat, float* yaw, float* pitch, float* roll)
{
	XMFLOAT4 q;
	XMStoreFloat4( &q, quat );

	float Nq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
	float _s = (Nq > 0.f) ? (2.f / Nq) : 0.f;

	float xs = q.x * _s, ys = q.y * _s, zs = q.z * _s;
	float wx = q.w * xs, wy = q.w * ys, wz = q.w * zs;
	float xx = q.x * xs, xy = q.x * ys, xz = q.x * zs;
	float yy = q.y * ys, yz = q.y * zs, zz = q.z * zs;

	float m[3][3] = { {1.f - (yy + zz),	xy - wz,					xz + wy},
					  { xy + wz,		1.f - (xx + zz),			yz - wx},
					  {xz - wy,			yz + wx,					1.f - (xx + yy)} };

	float cy = sqrtf(m[1][1] * m[1][1] + m[0][1] * m[0][1]);
	if (cy > 16 * FLT_EPSILON) {
		*yaw = atan2f(m[2][0], m[2][2]);
		*pitch = atan2f(-m[2][1], cy);
		*roll = atan2f(m[0][1], m[1][1]);
	}
	else {
		*yaw = atan2f(-m[0][2], m[0][0]);
		*pitch = atan2f(-m[2][1], cy);
		*roll = 0;
	}
}

void _quatToEuler(const XMVECTOR& quat, float* a0, float* a1, float* a2, int eulOrder)
{
	XMFLOAT4 q;
	XMStoreFloat4(&q, quat);

	float Nq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
	float _s = (Nq > 0.f) ? (2.f / Nq) : 0.f;

	float xs = q.x * _s, ys = q.y * _s, zs = q.z * _s;
	float wx = q.w * xs, wy = q.w * ys, wz = q.w * zs;
	float xx = q.x * xs, xy = q.x * ys, xz = q.x * zs;
	float yy = q.y * ys, yz = q.y * zs, zz = q.z * zs;

	float m[3][3] = { {1.f - (yy + zz),	xy - wz,					xz + wy},
					  { xy + wz,		1.f - (xx + zz),			yz - wx},
					  {xz - wy,			yz + wx,					1.f - (xx + yy)} };

	int i, j, k, h, s, n, f;

	EulGetOrd(eulOrder, i, j, k, h, n, s, f);

	if (s == EulRepYes) {
		float sy = sqrtf(m[i][j] * m[i][j] + m[i][k] * m[i][k]);
		if (sy > 16 * FLT_EPSILON) {
			*a0 = atan2f(m[i][j], m[i][k]);
			*a1 = atan2f(sy, m[i][i]);
			*a2 = atan2f(m[j][i], -m[k][i]);
		}
		else {
			*a0 = atan2f(-m[j][k], m[j][j]);
			*a1 = atan2f(sy, m[i][i]);
			*a2 = 0;
		}
	}
	else {
		float cy = sqrtf(m[i][i] * m[i][i] + m[j][i] * m[j][i]);
		if (cy > 16 * FLT_EPSILON) {
			*a0 = atan2f(m[k][j], m[k][k]);
			*a1 = atan2f(-m[k][i], cy);
			*a2 = atan2f(m[j][i], m[i][i]);
		}
		else {
			*a0 = atan2f(-m[j][k], m[j][j]);
			*a1 = atan2f(-m[k][i], cy);
			*a2 = 0;
		}
	}
	if (n == EulParOdd) { *a0 = -*a0; *a1 = -*a1; *a2 = -*a2; }
	if (f == EulFrmR) { float t = *a0; *a0 = *a2; *a2 = t; }
}

void _YawPitchRolltoQuat( XMVECTOR& quat, float yaw, float pitch, float roll )
{
	XMFLOAT4 q;
	XMStoreFloat4(&q, quat);

	float a[3], ti, tj, th, ci, cj, ch, si, sj, sh, cc, cs, sc, ss;

	ti = yaw * 0.5f; tj = pitch * 0.5f; th = roll * 0.5f;
	ci = cosf(ti);  cj = cosf(tj);  ch = cosf(th);
	si = sinf(ti);  sj = sinf(tj);  sh = sinf(th);
	cc = ci * ch; cs = ci * sh; sc = si * ch; ss = si * sh;

	a[1] = cj * sc - sj * cs;
	a[0] = cj * ss + sj * cc;
	a[2] = sj * sc - cj * cs;
	
	q.x = a[X]; q.y = a[Y]; q.z = a[Z];
	q.w = cj * cc + sj * ss;
}

void _EultoQuat( XMVECTOR& quat, float a0, float a1, float a2, int order)
{
	XMFLOAT4 q;
	XMStoreFloat4(&q, quat);

	float a[3], ti, tj, th, ci, cj, ch, si, sj, sh, cc, cs, sc, ss;
	int i, j, k, h, n, s, f;

	EulGetOrd( order, i, j, k, h, n, s, f );
	if (f == EulFrmR) { float t = a0; a0 = a2; a2 = t; }
	if (n == EulParOdd) a1 = -a1;
	ti = a0 * 0.5f; tj = a1 * 0.5f; th = a2 * 0.5f;
	ci = cosf(ti);  cj = cosf(tj);  ch = cosf(th);
	si = sinf(ti);  sj = sinf(tj);  sh = sinf(th);
	cc = ci * ch; cs = ci * sh; sc = si * ch; ss = si * sh;
	if (s == EulRepYes) {
		a[i] = cj * (cs + sc);	/* Could speed up with */
		a[j] = sj * (cc + ss);	/* trig identities. */
		a[k] = sj * (cs - sc);
		q.w = cj * (cc - ss);
	}
	else {
		a[i] = cj * sc - sj * cs;
		a[j] = cj * ss + sj * cc;
		a[k] = cj * cs - sj * sc;
		q.w = cj * cc + sj * ss;
	}
	if (n == EulParOdd) a[j] = -a[j];
	q.x = a[X]; q.y = a[Y]; q.z = a[Z];
}

/*

void _transformHierarchyVec3(const D3DXQUATERNION& q_parent, const D3DXVECTOR3& v_parent,
	const D3DXVECTOR3& v_child, D3DXVECTOR3& vout)
{
	// TRANSPOSE IT!

	//TRot = [1 - 2y2 - 2z2  2xy - 2wz    2xz + 2wy
	//		2xy + 2wz  1 - 2x2 - 2z2  2yz - 2wx
	//		2xz - 2wy    2yz + 2wx  1 - 2x2 - 2y2]
	
	// TODO: Need ASM optimization

	float x2 = q_parent.x * q_parent.x;
	float y2 = q_parent.y * q_parent.y;
	float z2 = q_parent.z * q_parent.z;

	float _2xy = 2 * q_parent.x * q_parent.y;
	float _2wz = 2 * q_parent.w * q_parent.z;
	float _2xz = 2 * q_parent.x * q_parent.z;
	float _2wy = 2 * q_parent.w * q_parent.y;
	float _2yz = 2 * q_parent.y * q_parent.z;
	float _2wx = 2 * q_parent.w * q_parent.x;

	float p11 = 1 - 2 * (y2 + z2);
	float p12 = _2xy + _2wz;
	float p13 = _2xz - _2wy;

	float p21 = _2xy - _2wz;
	float p22 = 1 - 2 * (x2 + z2);
	float p23 = _2yz + _2wx;

	float p31 = _2xz + _2wy;
	float p32 = _2yz - _2wx;
	float p33 = 1 - 2 * (x2 + y2);

	float p41 = v_parent.x;
	float p42 = v_parent.y;
	float p43 = v_parent.z;

	float c41 = v_child.x;
	float c42 = v_child.y;
	float c43 = v_child.z;
	float c44 = 1.f;

	vout.x = c41 * p11 + c42 * p21 + c43 * p31 + p41;
	vout.y = c41 * p12 + c42 * p22 + c43 * p32 + p42;
	vout.z = c41 * p13 + c42 * p23 + c43 * p33 + p43;
}

*/

/*
void _transformHierarchyFrameBones(gSkinBone* frameBones, int bone, int bonesNum)
{
	//=========================================================================
	//	OPTIMIAZATION: no matrix transforms for skinning
	//=========================================================================

	for (int i = 0; i < bonesNum; i++)
	{
		unsigned int parentId = frameBones[i].getParentId();

		if (parentId == bone)
		{
			if (parentId != -1)
			{
				D3DXVECTOR3 v_parent = frameBones[parentId].getPosition();
				D3DXQUATERNION q_parent = frameBones[parentId].getOrientation();
				D3DXVECTOR3 v_child = frameBones[i].getPosition();
				D3DXQUATERNION q_child = frameBones[i].getOrientation();

				D3DXQUATERNION q_transformedChild = q_child * q_parent;
				D3DXVECTOR3 v_transformedChild;
				_transformHierarchyVec3(q_parent, v_parent, v_child, v_transformedChild);

				//=========================================================================

				frameBones[i].setPosition(v_transformedChild);
				frameBones[i].setOrientation(q_transformedChild);
			}
			_transformHierarchyFrameBones( frameBones, i, bonesNum );
		}
	}
}
*/