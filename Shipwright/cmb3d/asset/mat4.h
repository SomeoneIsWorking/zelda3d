// Tiny 4x4 row-major matrix helpers shared by the OoT3D asset code (cmb + csab).
// Row-major: out_i = sum_j M[i*4+j]*v_j (v_3 = 1 for positions). Column-vector
// convention M*v, identical to tools/cmb.py / tools/csab.py.
#pragma once
#include <array>
#include <cmath>

namespace Zelda3D {

typedef std::array<float, 16> Mat4;

inline Mat4 matId() { Mat4 m{}; for (int i = 0; i < 4; i++) m[i * 4 + i] = 1; return m; }
inline Mat4 matMul(const Mat4& A, const Mat4& B) {
    Mat4 C{};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += A[i * 4 + k] * B[k * 4 + j];
            C[i * 4 + j] = s;
        }
    return C;
}
inline Mat4 matT(float x, float y, float z) { Mat4 m = matId(); m[3] = x; m[7] = y; m[11] = z; return m; }
inline Mat4 matS(float x, float y, float z) { Mat4 m = matId(); m[0] = x; m[5] = y; m[10] = z; return m; }
inline Mat4 matRx(float a) { float c = cosf(a), s = sinf(a); Mat4 m = matId(); m[5] = c; m[6] = -s; m[9] = s; m[10] = c; return m; }
inline Mat4 matRy(float a) { float c = cosf(a), s = sinf(a); Mat4 m = matId(); m[0] = c; m[2] = s; m[8] = -s; m[10] = c; return m; }
inline Mat4 matRz(float a) { float c = cosf(a), s = sinf(a); Mat4 m = matId(); m[0] = c; m[1] = -s; m[4] = s; m[5] = c; return m; }
inline void matApplyPos(const Mat4& M, const float* p, float* out) {
    for (int i = 0; i < 3; i++) out[i] = M[i * 4 + 0] * p[0] + M[i * 4 + 1] * p[1] + M[i * 4 + 2] * p[2] + M[i * 4 + 3];
}
inline void matApplyDir(const Mat4& M, const float* v, float* out) {
    for (int i = 0; i < 3; i++) out[i] = M[i * 4 + 0] * v[0] + M[i * 4 + 1] * v[1] + M[i * 4 + 2] * v[2];
}
// General 4x4 inverse via Gauss-Jordan (bind matrices may carry non-uniform scale).
// Returns identity if singular.
inline Mat4 matInverse(const Mat4& M) {
    double a[4][8];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) { a[i][j] = M[i * 4 + j]; a[i][4 + j] = (i == j) ? 1.0 : 0.0; }
    for (int col = 0; col < 4; col++) {
        int piv = col;
        for (int r = col + 1; r < 4; r++) if (std::fabs(a[r][col]) > std::fabs(a[piv][col])) piv = r;
        if (std::fabs(a[piv][col]) < 1e-12) return matId();
        for (int j = 0; j < 8; j++) { double t = a[col][j]; a[col][j] = a[piv][j]; a[piv][j] = t; }
        double d = a[col][col];
        for (int j = 0; j < 8; j++) a[col][j] /= d;
        for (int r = 0; r < 4; r++) {
            if (r == col) continue;
            double f = a[r][col];
            for (int j = 0; j < 8; j++) a[r][j] -= f * a[col][j];
        }
    }
    Mat4 out{};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) out[i * 4 + j] = (float)a[i][4 + j];
    return out;
}

} // namespace Zelda3D
