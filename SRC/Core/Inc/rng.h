#ifndef __RNG_H__
#define __RNG_H__
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

/* 软件伪随机数，适用于游戏逻辑，不用于密码学或安全用途。 */
void Rng_init(void);
uint32_t Rng_get_int(void);
/* 整数范围包含 min 和 max；若 min>max 则自动交换。 */
int Rng_get_range_int(int min,int max);
/* 浮点范围为 [min,max)；若 min>max 则自动交换。 */
float Rng_get_range_float(float min,float max);

#ifdef __cplusplus
}
#endif
#endif
