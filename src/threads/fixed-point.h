const int q = 14;
const int f = 1 << q;

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

/* Converts an integer to a fixed-point number. */
fixed_point_t int_to_fixed_point(int n) {
    return n * f;
}

/* Converts a fixed-point number to an integer (rounds towards zero). */
int fixed_point_to_int(fixed_point_t x) {
    return x / f;
}

/* Converts a fixed-point number to an integer (rounds to the nearest integer). */
int fixed_point_to_int_nearest(fixed_point_t x) {
    if (x >= 0)
        return (x + f / 2) / f;
    else
        return (x - f / 2) / f;
}

/* Adds two fixed-point numbers. */
fixed_point_t add_fixed_point(fixed_point_t x, fixed_point_t y) {
    return x + y;
}

/* Subtracts one fixed-point number from another. */
fixed_point_t sub_fixed_point(fixed_point_t x, fixed_point_t y) {
    return x - y;
}

/* Adds an integer to a fixed-point number. */
fixed_point_t add_int_to_fixed_point(fixed_point_t x, int n) {
    return x + n * f;
}

/* Subtracts an integer from a fixed-point number. */
fixed_point_t sub_int_from_fixed_point(fixed_point_t x, int n) {
    return x - n * f;
}

/* Multiplies two fixed-point numbers. */
fixed_point_t mult_fixed_point(fixed_point_t x, fixed_point_t y) {
    return ((int64_t) x) * y / f;
}

/* Multiplies a fixed-point number by an integer. */
fixed_point_t mult_fixed_point_by_int(fixed_point_t x, int n) {
    return x * n;
}

/* Divides one fixed-point number by another. */
fixed_point_t div_fixed_point(fixed_point_t x, fixed_point_t y) {
    return ((int64_t) x) * f / y;
}

/* Divides a fixed-point number by an integer. */
fixed_point_t div_fixed_point_by_int(fixed_point_t x, int n) {
    return x / n;
}