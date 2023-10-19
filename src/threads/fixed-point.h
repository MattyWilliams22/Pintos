#include <stdint.h>

static const int q = 14;
static const int f = 1 << q;

typedef int32_t fixed_point_t; 

fixed_point_t int_to_fixed_point(int n);
int fixed_point_to_int(fixed_point_t x);
int fixed_point_to_int_nearest(fixed_point_t x);
fixed_point_t add_fixed_point(fixed_point_t x, fixed_point_t y);
fixed_point_t sub_fixed_point(fixed_point_t x, fixed_point_t y);
fixed_point_t add_int_to_fixed_point(fixed_point_t x, int n);
fixed_point_t sub_int_from_fixed_point(fixed_point_t x, int n);
fixed_point_t mult_fixed_point(fixed_point_t x, fixed_point_t y);
fixed_point_t mult_fixed_point_by_int(fixed_point_t x, int n);
fixed_point_t div_fixed_point(fixed_point_t x, fixed_point_t y);
fixed_point_t div_fixed_point_by_int(fixed_point_t x, int n);