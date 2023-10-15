#define q 14
#define f (1<<q)

typedef int32_t fixed_point 

fixed_point int_to_fixed_point(int n);
int fixed_point_to_int(fixed_point x);
int fixed_point_to_int_nearest(fixed_point x);
fixed_point add_fixed_point(fixed_point x, fixed_point y);
fixed_point sub_fixed_point(fixed_point x, fixed_point y);
fixed_point add_int_to_fixed_point(fixed_point x, int n);
fixed_point sub_int_from_fixed_point(fixed_point x, int n);
fixed_point mult_fixed_point(fixed_point x, fixed_point y);
fixed_point mult_fixed_point_by_int(fixed_point x, int n);
fixed_point div_fixed_point(fixed_point x, fixed_point y);
fixed_point div_fixed_point_by_int(fixed_point x, int n);

/* Converts an integer to a fixed-point number. */
fixed_point int_to_fixed_point(int n) {
    return n * f;
}

/* Converts a fixed-point number to an integer (rounds towards zero). */
int fixed_point_to_int(fixed_point x) {
    return x / f;
}

/* Converts a fixed-point number to an integer (rounds to the nearest integer). */
int fixed_point_to_int_nearest(fixed_point x) {
    if (x >= 0)
        return (x + f / 2) / f;
    else
        return (x - f / 2) / f;
}

/* Adds two fixed-point numbers. */
fixed_point add_fixed_point(fixed_point x, fixed_point y) {
    return x + y;
}

/* Subtracts one fixed-point number from another. */
fixed_point sub_fixed_point(fixed_point x, fixed_point y) {
    return x - y;
}

/* Adds an integer to a fixed-point number. */
fixed_point add_int_to_fixed_point(fixed_point x, int n) {
    return x + n * f;
}

/* Subtracts an integer from a fixed-point number. */
fixed_point sub_int_from_fixed_point(fixed_point x, int n) {
    return x - n * f;
}

/* Multiplies two fixed-point numbers. */
fixed_point mult_fixed_point(fixed_point x, fixed_point y) {
    return ((int64_t) x) * y / f;
}

/* Multiplies a fixed-point number by an integer. */
fixed_point mult_fixed_point_by_int(fixed_point x, int n) {
    return x * n;
}

/* Divides one fixed-point number by another. */
fixed_point div_fixed_point(fixed_point x, fixed_point y) {
    return ((int64_t) x) * f / y;
}

/* Divides a fixed-point number by an integer. */
fixed_point div_fixed_point_by_int(fixed_point x, int n) {
    return x / n;
}