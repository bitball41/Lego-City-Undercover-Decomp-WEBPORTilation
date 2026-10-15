#include "numath/matrix.h"
#include "numath/scalar.h"
#include "numath/vector.h"

#include <cmath>
#include <cstring>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kOldTurn = 65536.f;

float OldToRad(int angle) {
    return static_cast<float>(angle) * (2.f * kPi / kOldTurn);
}

void SetRow(NUMTX* m, int row, float a, float b, float c, float d) {
    m->m[row][0] = a;
    m->m[row][1] = b;
    m->m[row][2] = c;
    m->m[row][3] = d;
}

NUVEC Cross(const NUVEC& a, const NUVEC& b) {
    NUVEC o;
    o.x = a.y * b.z - a.z * b.y;
    o.y = a.z * b.x - a.x * b.z;
    o.z = a.x * b.y - a.y * b.x;
    return o;
}

float Dot(const NUVEC& a, const NUVEC& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

NUVEC Norm(NUVEC v) {
    float m = std::sqrt(Dot(v, v));
    if (m > 1.0e-8f) {
        v.x /= m;
        v.y /= m;
        v.z /= m;
    }
    return v;
}

}  // namespace

unsigned int fseed = 1;

float NuSinOld(int angle) {
    return std::sin(OldToRad(angle));
}

float NuSinf(float angle) {
    return std::sin(angle);
}
float NuCosf(float angle) {
    return std::cos(angle);
}
void NuSinCos(float angle, float* outSin, float* outCos) {
    *outSin = std::sin(angle);
    *outCos = std::cos(angle);
}

uint16_t NuFloatToHalf(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t exp = static_cast<int32_t>((bits >> 23) & 0xff) - 127 + 15;
    uint32_t mant = bits & 0x7fffffu;
    if (exp <= 0) {
        if (exp < -10) {
            return static_cast<uint16_t>(sign);
        }
        mant |= 0x800000u;
        uint32_t shift = static_cast<uint32_t>(1 - exp);
        uint32_t halfMant = mant >> (shift + 13);
        return static_cast<uint16_t>(sign | halfMant);
    }
    if (exp >= 31) {
        return static_cast<uint16_t>(sign | 0x7c00u);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
}

void NuMtxSetZero(NUMTX* out) {
    std::memset(out, 0, sizeof(NUMTX));
}

void NuMtxSetIdentity(NUMTX* out) {
    NuMtxSetZero(out);
    out->m[0][0] = 1.f;
    out->m[1][1] = 1.f;
    out->m[2][2] = 1.f;
    out->m[3][3] = 1.f;
}

void NuMtxSetTranslation(NUMTX* out, NUVEC const* translation) {
    NuMtxSetIdentity(out);
    out->m[3][0] = translation->x;
    out->m[3][1] = translation->y;
    out->m[3][2] = translation->z;
}

void NuMtxGetTranslation(NUMTX const* mtx, NUVEC* outTranslation) {
    outTranslation->x = mtx->m[3][0];
    outTranslation->y = mtx->m[3][1];
    outTranslation->z = mtx->m[3][2];
}

void NuMtxSetRotationYRads(NUMTX* out, float rads) {
    float c = std::cos(rads);
    float s = std::sin(rads);
    NuMtxSetIdentity(out);
    out->m[0][0] = c;
    out->m[0][2] = -s;
    out->m[2][0] = s;
    out->m[2][2] = c;
}

void NuMtxMul(NUMTX* out, NUMTX const* lhs, NUMTX const* rhs) {
    NUMTX tmp;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            tmp.m[r][c] = lhs->m[r][0] * rhs->m[0][c] + lhs->m[r][1] * rhs->m[1][c]
                          + lhs->m[r][2] * rhs->m[2][c] + lhs->m[r][3] * rhs->m[3][c];
        }
    }
    *out = tmp;
}

void NuMtxSetPerspectiveD3D(NUMTX* out, float fovY, float aspect, float zNear, float zFar) {
    NuMtxSetZero(out);
    float yScale = 1.f / std::tan(fovY * 0.5f);
    float xScale = aspect == 0.f ? yScale : yScale / aspect;
    float span = zFar - zNear;
    if (span == 0.f) {
        span = 1.f;
    }
    out->m[0][0] = xScale;
    out->m[1][1] = yScale;
    out->m[2][2] = zFar / span;
    out->m[2][3] = 1.f;
    out->m[3][2] = -zNear * zFar / span;
}

void NuMtxLookAtD3D(NUMTX* out, NUVEC const* eye, NUVEC const* at, NUVEC const* up) {
    NUVEC forward = Norm(NUVEC{at->x - eye->x, at->y - eye->y, at->z - eye->z});
    NUVEC right = Norm(Cross(*up, forward));
    NUVEC realUp = Cross(forward, right);
    NuMtxSetIdentity(out);
    SetRow(out, 0, right.x, realUp.x, forward.x, 0.f);
    SetRow(out, 1, right.y, realUp.y, forward.y, 0.f);
    SetRow(out, 2, right.z, realUp.z, forward.z, 0.f);
    SetRow(out, 3, -Dot(right, *eye), -Dot(realUp, *eye), -Dot(forward, *eye), 1.f);
}
