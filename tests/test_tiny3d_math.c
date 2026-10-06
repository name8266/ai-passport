#include <assert.h>
#include <stdint.h>
#include "tiny3d_math.h"

static int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

int main(void)
{
    t3_quat_t q = t3_quat_from_seed(0x12345678u);
    uint64_t sum = (uint64_t)((int64_t)q.w * q.w) +
                   (uint64_t)((int64_t)q.x * q.x) +
                   (uint64_t)((int64_t)q.y * q.y) +
                   (uint64_t)((int64_t)q.z * q.z);
    uint32_t mag = t3_isqrt_u64(sum);
    assert(iabs32((int32_t)mag - T3_Q14_ONE) <= 2);

    t3_vec3_t v = {256, -512, 768};
    t3_vec3_t same = t3_quat_rotate_q8(t3_quat_identity(), v);
    assert(same.x == v.x && same.y == v.y && same.z == v.z);

    t3_quat_t before = q;
    t3_quat_integrate_60hz(&q, (t3_vec3_t){1200, -800, 1600});
    assert(q.w != before.w || q.x != before.x || q.y != before.y || q.z != before.z);

    sum = (uint64_t)((int64_t)q.w * q.w) +
          (uint64_t)((int64_t)q.x * q.x) +
          (uint64_t)((int64_t)q.y * q.y) +
          (uint64_t)((int64_t)q.z * q.z);
    mag = t3_isqrt_u64(sum);
    assert(iabs32((int32_t)mag - T3_Q14_ONE) <= 2);
    return 0;
}
