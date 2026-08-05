#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    const size_t count = 2000000U;
    int32_t *values = malloc(count * sizeof(*values));
    if (values == NULL)
        return 2;

    for (size_t index = 0; index < count; ++index)
        values[index] = (int32_t)(index % 1024U);

    int64_t total = 0;
    for (size_t index = 0; index < count; ++index)
        total += values[index];

    printf("%" PRId64 "\n", total);
    free(values);
    return 0;
}
