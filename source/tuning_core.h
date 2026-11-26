// tuning core: builds the 128-word note table from a tuning block and the unit's own stock table.
// Portable: the same code on the unit and the computer. No includes, no division, no 64-bit arithmetic, no floats,
// no unaligned word access (words are assembled from bytes).
//
// Tuning block (640 bytes): src[128] bytes, then ratio[128] little-endian u32, Q2.30 (TC_ONE = 1.0).
// File ".oft" (648 bytes): "OFT1", the block, CRC-32 (zlib) of bytes 0..643, little-endian.
// Root k (0..11): key n, m = n - k; m < 0 or src[m] + k > 127: stock[n]; else floor(stock[src[m] + k] * ratio[m] / 2^30).
// Valid only if every src <= 127 and every entry is in 1..TC_MOST.
// Detune: every entry times a Q2.30 ratio (tc_detune), again valid only in 1..TC_MOST.
#ifndef TUNING_CORE_H
#define TUNING_CORE_H

#ifndef TC_TYPES
#define TC_TYPES
typedef unsigned char u8;
typedef unsigned int u32;
#endif

enum { TC_NOTES = 128, TC_BLOCK = 640, TC_FILE = 648, TC_ONE = 0x40000000, TC_MOST = 0x3fffffff };

// bitwise zlib CRC-32, no table
static inline u32 tc_crc32(const u8* p, u32 n) {
    u32 c = 0xffffffffu;
    for (u32 i = 0; i < n; ++i) {
        c ^= p[i];
        for (int b = 0; b < 8; ++b) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static inline u32 tc_get32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

// *out = floor(a * ratio / 2^30); the 64-bit product as hi:lo from four 16x16 products. 0 when it does not fit 32 bits.
static inline int tc_mul_q30(u32 a, u32 ratio, u32* out) {
    const u32 al = a & 0xffffu, ah = a >> 16, rl = ratio & 0xffffu, rh = ratio >> 16;
    const u32 ll = al * rl, lh = al * rh, hl = ah * rl, hh = ah * rh;
    const u32 mid = lh + hl;
    const u32 c1 = mid < lh ? 0x10000u : 0u;          // carry out of mid is worth 2^48 = 2^16 in hi
    const u32 lo = ll + (mid << 16);
    const u32 c2 = lo < ll ? 1u : 0u;
    const u32 hi = hh + (mid >> 16) + c1 + c2;        // cannot wrap: the product is below 2^64
    if (hi >> 30) return 0;
    *out = (hi << 2) | (lo >> 30);
    return 1;
}

static inline int tc_file_ok(const u8* file, int bytes) {
    if (bytes != TC_FILE) return 0;
    if (file[0] != 'O' || file[1] != 'F' || file[2] != 'T' || file[3] != '1') return 0;
    return tc_crc32(file, TC_FILE - 4) == tc_get32(file + TC_FILE - 4);
}

// fills out[128]; 1 when valid, 0 otherwise (out is then not to be used)
static inline int tc_build(const u32* stock, const u8* block, int root, u32* out) {
    if (root < 0 || root > 11) return 0;
    for (int n = 0; n < TC_NOTES; ++n)
        if (block[n] > 127) return 0;
    for (int n = 0; n < TC_NOTES; ++n) {
        const int m = n - root;
        u32 e = stock[n];
        if (m >= 0) {
            const int s = (int)block[m] + root;
            if (s <= 127 && !tc_mul_q30(stock[s], tc_get32(block + TC_NOTES + 4 * m), &e)) return 0;
        }
        if (e < 1u || e > (u32)TC_MOST) return 0;
        out[n] = e;
    }
    return 1;
}

// table[n] = floor(table[n] * ratio / 2^30), all 128 in place; 1 when every result is in 1..TC_MOST, else 0 and the
// table is left as it was. ratio TC_ONE leaves every entry as it is.
static inline int tc_detune(u32* table, u32 ratio) {
    u32 e;
    for (int n = 0; n < TC_NOTES; ++n)
        if (!tc_mul_q30(table[n], ratio, &e) || e < 1u || e > (u32)TC_MOST) return 0;
    for (int n = 0; n < TC_NOTES; ++n) {
        tc_mul_q30(table[n], ratio, &e);
        table[n] = e;
    }
    return 1;
}

#endif
