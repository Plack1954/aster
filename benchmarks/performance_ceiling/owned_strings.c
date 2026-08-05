#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static unsigned decimal_length(uint64_t value)
{
    unsigned length = 1U;
    while (value >= UINT64_C(10)) {
        value /= UINT64_C(10);
        ++length;
    }
    return length;
}

int main(void)
{
    uint64_t total = 0;
    int active = 1;
    for (int64_t index = 0; index < INT64_C(500000); ++index) {
        total += UINT64_C(26);
        total += decimal_length((uint64_t)index);
        total += active != 0 ? UINT64_C(4) : UINT64_C(5);
        total += decimal_length((uint64_t)(index * INT64_C(3)));
        active = !active;
    }
    printf("%" PRIu64 "\n", total);
    return 0;
}
