#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    int64_t value = 1;
    for (int64_t iteration = 0; iteration < INT64_C(50000000); ++iteration) {
        value += iteration % INT64_C(97) + INT64_C(1);
        if (value > INT64_C(1000000000))
            value -= INT64_C(1000000000);
    }
    printf("%" PRId64 "\n", value);
    return 0;
}
