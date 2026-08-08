#ifndef ASTER_VARIATION_ENTRY_H
#define ASTER_VARIATION_ENTRY_H

/* Required ABI name for Clang's coverage-guided runtime. */
#define ASTER_VARIATION_JOIN_INNER(left, right) left##right
#define ASTER_VARIATION_JOIN(left, right) \
    ASTER_VARIATION_JOIN_INNER(left, right)
#define ASTER_VARIATION_ENTRY \
    ASTER_VARIATION_JOIN(LLVMFuz, zerTestOneInput)

#endif
