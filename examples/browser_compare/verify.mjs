import {readFile} from "node:fs/promises";

if (process.argv.length !== 3)
    throw new Error("expected the comparison Wasm path");
const bytes = await readFile(process.argv[2]);
let memory;
const {instance} = await WebAssembly.instantiate(bytes, {
    aster: {
        trap(pointer, length) {
            const bytes = new Uint8Array(memory.buffer, pointer, length);
            throw new Error(new TextDecoder().decode(bytes));
        },
        now_ms() { return BigInt(Date.now()); }
    }
});
memory = instance.exports.memory;
const exports = instance.exports;
function takeString(handle) {
    try {
        const pointer = Number(exports.aster_export_string_data(handle));
        const length = Number(exports.aster_export_string_length(handle));
        return new TextDecoder().decode(
            new Uint8Array(memory.buffer, pointer, length)
        );
    } finally {
        exports.aster_export_string_drop(handle);
    }
}

const patch = Number(exports.aster_export_Create1000(0n));
const nextId = exports.aster_export_Create1000_result_l_nextId(patch);
const htmlHandle = Number(exports.aster_export_Create1000_result_h_rows(patch));
const stringHandle = Number(exports.aster_export_html_render(htmlHandle));
try {
    const pointer = Number(exports.aster_export_string_data(stringHandle));
    const length = Number(exports.aster_export_string_length(stringHandle));
    const html = new TextDecoder().decode(
        new Uint8Array(memory.buffer, pointer, length)
    );
    if (nextId !== 1000n)
        throw new Error(`wrong next row id: ${nextId}`);
    if ((html.match(/<tr id="row-/g) ?? []).length !== 1000)
        throw new Error("Wasm did not render 1,000 keyed rows");
    if (!html.includes("click|RemoveRow|r|s:key"))
        throw new Error("row removal metadata is missing");
} finally {
    exports.aster_export_string_drop(stringHandle);
    exports.aster_export_Create1000_result_drop(patch);
}
const updatedHtml = Number(exports.aster_export_UpdateEvery10th());
const updatedString = Number(exports.aster_export_html_render(updatedHtml));
const updates = takeString(updatedString);
if ((updates.match(/<tr id="row-/g) ?? []).length !== 100 ||
    !updates.includes("row 990 !!!"))
    throw new Error("sparse row update output is wrong");

const swap = Number(exports.aster_export_SwapRows());
try {
    const first = takeString(Number(
        exports.aster_export_SwapRows_result_o_first(swap)
    ));
    const second = takeString(Number(
        exports.aster_export_SwapRows_result_o_second(swap)
    ));
    if (first !== "row-1" || second !== "row-998")
        throw new Error(`wrong swap keys: ${first}, ${second}`);
} finally {
    exports.aster_export_SwapRows_result_drop(swap);
}

const clear = Number(exports.aster_export_ClearRows());
try {
    if (exports.aster_export_ClearRows_result_b_clear(clear) === 0)
        throw new Error("clear operation is not enabled");
} finally {
    exports.aster_export_ClearRows_result_drop(clear);
}

console.log("browser comparison Wasm rendered and mutated 1,000 keyed rows");
