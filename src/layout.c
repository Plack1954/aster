#include "lang/lang.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

void lang_target_host(LangTargetInfo *out_target) {
    if (out_target == NULL) return;
    uint16_t endian_probe = UINT16_C(1);
    const unsigned char *bytes =
        (const unsigned char *)&endian_probe;
    *out_target = (LangTargetInfo){
        .pointer_size=(uint8_t)sizeof(void *),
        .pointer_alignment=(uint8_t)_Alignof(void *),
        .enum_tag_size=4U,
        .enum_tag_alignment=4U,
        .endianness=bytes[0] == 1U
                   ? LANG_ENDIAN_LITTLE : LANG_ENDIAN_BIG,
        .c_abi_supported=CHAR_BIT == 8 &&
                         sizeof(void *) <= UINT8_MAX &&
                         _Alignof(void *) <= UINT8_MAX
    };
}
