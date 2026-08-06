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
console.log("browser comparison Wasm rendered 1,000 keyed rows");
