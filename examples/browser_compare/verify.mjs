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

function render(component) {
    const html = Number(
        exports.aster_export_component_BenchmarkTable_render(component)
    );
    return takeString(Number(exports.aster_export_html_render(html)));
}

function beginMutations(component) {
    exports.aster_export_component_BenchmarkTable_mutations_begin(component);
}

function endMutations(component) {
    return Number(
        exports.aster_export_component_BenchmarkTable_mutations_end(component)
    );
}

const component = Number(
    exports.aster_export_component_BenchmarkTable_new()
);
try {
    beginMutations(component);
    exports.aster_export_BenchmarkTable_Create1000(component);
    if (endMutations(component) !== 2 ||
        exports.aster_export_component_BenchmarkTable_mutation_kind(
            component, 0
        ) !== 4 ||
        exports.aster_export_component_BenchmarkTable_mutation_kind(
            component, 1
        ) !== 1 ||
        exports.aster_export_component_BenchmarkTable_mutation_count(
            component, 1
        ) !== 1000)
        throw new Error("compiler-internal create journal is wrong");
    let html = render(component);
    if ((html.match(/data-aster-key="row-/g) ?? []).length !== 1000)
        throw new Error("Wasm did not render 1,000 keyed rows");
    if (!html.includes("BenchmarkTable_DeleteRow|v|x:BenchmarkTable|l:key"))
        throw new Error("native row removal metadata is missing");

    beginMutations(component);
    exports.aster_export_BenchmarkTable_UpdateEvery10th(component);
    if (endMutations(component) !== 100 ||
        exports.aster_export_component_BenchmarkTable_mutation_kind(
            component, 0
        ) !== 2 ||
        exports.aster_export_component_BenchmarkTable_mutation_index(
            component, 99
        ) !== 990)
        throw new Error("compiler-internal sparse update journal is wrong");
    html = render(component);
    if (!html.includes("row 990 !!!"))
        throw new Error("sparse row update output is wrong");

    beginMutations(component);
    exports.aster_export_BenchmarkTable_SwapRows(component);
    if (endMutations(component) !== 2)
        throw new Error("compiler-internal swap journal is wrong");
    html = render(component);
    const first = html.indexOf('data-aster-key="row-998"');
    const second = html.indexOf('data-aster-key="row-1"');
    if (first < 0 || second < 0 || first >= second)
        throw new Error("native keyed swap output is wrong");
    const skipped = Number(
        exports.aster_export_component_BenchmarkTable_render_skip(
            component, 1000
        )
    );
    const skippedHtml = takeString(Number(
        exports.aster_export_html_render(skipped)
    ));
    if (skippedHtml.includes("data-aster-key="))
        throw new Error("internal keyed render suppression is wrong");

    beginMutations(component);
    exports.aster_export_BenchmarkTable_ClearRows(component);
    if (endMutations(component) !== 1 ||
        exports.aster_export_component_BenchmarkTable_mutation_kind(
            component, 0
        ) !== 4)
        throw new Error("compiler-internal clear journal is wrong");
    html = render(component);
    if (html.includes("data-aster-key="))
        throw new Error("native keyed clear output is wrong");
} finally {
    exports.aster_export_component_BenchmarkTable_drop(component);
}

if (Object.keys(exports).some((name) =>
    name.startsWith("aster_export_projection_batch_") ||
    name.includes("result_r_") || name.includes("result_w_") ||
    name.includes("result_c_")))
    throw new Error("legacy structural result ABI was emitted");

console.log("browser comparison Wasm rendered and mutated 1,000 keyed rows");
