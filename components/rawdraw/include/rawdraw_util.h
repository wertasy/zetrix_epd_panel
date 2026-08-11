#ifndef RAWDRAW_UTIL_H_
#define RAWDRAW_UTIL_H_

#define RD_MIN(a, b) ((a) < (b) ? (a) : (b))
#define RD_MAX(a, b) ((a) > (b) ? (a) : (b))
#define RD_CLAMP(v, lo, hi) RD_MIN(RD_MAX((v), (lo)), (hi))
#define RD_MIN_MAX(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

#endif /* RAWDRAW_UTIL_H_ */

#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif
