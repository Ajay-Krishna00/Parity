#pragma once
#include <cstdint>

// Q16.16 fixed-point number.
//
// Why fixed-point instead of float? Floating-point results can differ across
// CPUs, compilers, and optimization levels (x87 vs SSE, FMA contraction, etc).
// Rollback requires that re-simulating identical inputs gives a BYTE-IDENTICAL
// result on both peers, so the simulation must use only integer arithmetic,
// which is exactly defined by the C++ standard. Q16.16 = 16 integer bits +
// 16 fractional bits packed into one int32_t.
namespace parity {

struct Fixed {
    int32_t raw;

    static const int32_t FRAC_BITS = 16;
    static const int32_t ONE       = 1 << FRAC_BITS;

    Fixed() : raw(0) {}
    explicit Fixed(int32_t r) : raw(r) {}

    static Fixed fromInt(int32_t v)  { return Fixed(v << FRAC_BITS); }
    static Fixed fromRaw(int32_t r)  { return Fixed(r); }
    // Build a fraction without ever touching a float (e.g. fromFraction(1,4) = 0.25).
    static Fixed fromFraction(int32_t num, int32_t den) {
        return Fixed((int32_t)(((int64_t)num << FRAC_BITS) / den));
    }

    int32_t toInt() const { return raw >> FRAC_BITS; }

    Fixed operator+(Fixed o) const { return Fixed(raw + o.raw); }
    Fixed operator-(Fixed o) const { return Fixed(raw - o.raw); }
    Fixed operator-()        const { return Fixed(-raw); }
    // 64-bit intermediate prevents overflow, then shift back into Q16.16.
    Fixed operator*(Fixed o) const {
        return Fixed((int32_t)(((int64_t)raw * o.raw) >> FRAC_BITS));
    }
    Fixed operator/(Fixed o) const {
        return Fixed((int32_t)(((int64_t)raw << FRAC_BITS) / o.raw));
    }

    Fixed& operator+=(Fixed o) { raw += o.raw; return *this; }
    Fixed& operator-=(Fixed o) { raw -= o.raw; return *this; }

    bool operator<(Fixed o)  const { return raw <  o.raw; }
    bool operator>(Fixed o)  const { return raw >  o.raw; }
    bool operator<=(Fixed o) const { return raw <= o.raw; }
    bool operator>=(Fixed o) const { return raw >= o.raw; }
    bool operator==(Fixed o) const { return raw == o.raw; }
};

inline Fixed fxAbs(Fixed v) { return v.raw < 0 ? Fixed(-v.raw) : v; }

// Convenience literal: fx(3) == 3.0 in Q16.16.
inline Fixed fx(int32_t v) { return Fixed::fromInt(v); }

} // namespace parity
