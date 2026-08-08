#include "vm_internal.h"
#include "variation_entry.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int ASTER_VARIATION_ENTRY(const uint8_t *data, size_t size);

static uint32_t variation_u32(
    const uint8_t *data, size_t size, size_t offset) {
    uint32_t result = 0U;
    for (size_t i = 0U; i < sizeof(result) && offset + i < size; ++i)
        result |= (uint32_t)data[offset + i] << (i * 8U);
    return result;
}

int ASTER_VARIATION_ENTRY(const uint8_t *data, size_t size) {
    if (size > 64U * 1024U) return 0;
    size_t code_count = size == 0U ? 0U : (size + 8U) / 9U;
    Instruction *code = calloc(
        code_count == 0U ? 1U : code_count, sizeof(*code));
    BytecodeCallSite *call_sites = calloc(
        code_count == 0U ? 1U : code_count, sizeof(*call_sites));
    if (code == NULL || call_sites == NULL) {
        free(code);
        free(call_sites);
        return 0;
    }
    for (size_t i = 0U; i < code_count; ++i) {
        size_t offset = i * 9U;
        code[i].op = (OpCode)(offset < size ? data[offset] : 0U);
        code[i].a = (int32_t)variation_u32(data, size, offset + 1U);
        code[i].b = (int32_t)variation_u32(data, size, offset + 5U);
    }
    size_t arity = size == 0U ? 0U : data[0] % 8U;
    ParameterMode *modes = calloc(
        arity == 0U ? 1U : arity, sizeof(*modes));
    if (modes == NULL) {
        free(call_sites);
        free(code);
        return 0;
    }
    for (size_t i = 0U; i < arity; ++i)
        modes[i] = (ParameterMode)(i + 1U < size ? data[i + 1U] : 0U);
    BytecodeFunction function = {
        .name="variation",
        .code=code,
        .call_sites=call_sites,
        .code_count=code_count,
        .code_capacity=code_count,
        .arity=arity,
        .parameter_modes=modes,
        .local_count=size < 3U ? 0U : data[2] % 32U
    };
    Constant constant = {
        .value={.tag=LANG_VALUE_I64,
                .as.i64=(int64_t)variation_u32(data, size, 3U)}
    };
    BytecodeModule module = {
        .functions=&function,
        .function_count=1U,
        .constants=&constant,
        .constant_count=1U
    };
    (void)vm_verify_bytecode_module(&module);
    free(modes);
    free(call_sites);
    free(code);
    return 0;
}
