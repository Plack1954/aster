#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static inline int64_t mix(int64_t value, int64_t iteration)
{
    int64_t next = value + iteration + INT64_C(1);
    return next > INT64_C(1000000000)
        ? next - INT64_C(1000000000)
        : next;
}

int main(void)
{
    int64_t value = 1;
    for (int64_t iteration = 0; iteration < INT64_C(20000000); ++iteration)
        value = mix(value, iteration);
    printf("%" PRId64 "\n", value);
    return 0;
}
