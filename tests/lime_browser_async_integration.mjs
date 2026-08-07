import {readFile} from "node:fs/promises";

if (process.argv.length !== 3)
    throw new Error("expected the Lime browser Wasm path");

const bytes = await readFile(process.argv[2]);
let memory;
const imports = {
    aster: {
        trap(pointer, length) {
            const view = new Uint8Array(memory.buffer, pointer, length);
            throw new Error(new TextDecoder().decode(view));
        },
        now_ms() {
            return BigInt(Date.now());
        }
    }
};
const {instance} = await WebAssembly.instantiate(bytes, imports);
memory = instance.exports.memory;

const start = instance.exports.aster_export_IncrementLater;
const status = instance.exports.aster_export_task_status;
const result = instance.exports.aster_export_IncrementLater_task_result;
const drop = instance.exports.aster_export_task_drop;
if (typeof start !== "function" || typeof status !== "function" ||
    typeof result !== "function" || typeof drop !== "function")
    throw new Error("async browser Task exports are incomplete");

const task = Number(start(41n));
if (task === 0) throw new Error("async browser handler returned a null Task");
if (status(task) !== 0)
    throw new Error("Task.Delay browser handler did not initially suspend");

let state = 0;
for (let attempt = 0; attempt < 100 && state === 0; ++attempt) {
    await new Promise((resolve) => setTimeout(resolve, 1));
    state = status(task);
}
if (state !== 1)
    throw new Error(`async browser Task did not succeed: ${state}`);
if (result(task) !== 42n)
    throw new Error("async browser Task returned the wrong result");
drop(task);

const fail = instance.exports.aster_export_FailLater;
const taskError = instance.exports.aster_export_task_error;
const stringData = instance.exports.aster_export_string_data;
const stringLength = instance.exports.aster_export_string_length;
const stringDrop = instance.exports.aster_export_string_drop;
if (typeof fail !== "function" || typeof taskError !== "function")
    throw new Error("async browser Task fault exports are incomplete");
const failedTask = Number(fail(0n));
let failedState = 0;
for (let attempt = 0; attempt < 100 && failedState === 0; ++attempt) {
    await new Promise((resolve) => setTimeout(resolve, 1));
    failedState = status(failedTask);
}
if (failedState !== 2)
    throw new Error(`async browser Task did not fault: ${failedState}`);
const errorHandle = Number(taskError(failedTask));
try {
    const pointer = Number(stringData(errorHandle));
    const length = Number(stringLength(errorHandle));
    const message = new TextDecoder().decode(
        new Uint8Array(memory.buffer, pointer, length)
    );
    if (message !== "browser async failure")
        throw new Error(`unexpected async browser error: ${message}`);
} finally {
    stringDrop(errorHandle);
    drop(failedTask);
}

function projectionPart(typeName, fieldIndex) {
    let value = 14695981039346656037n;
    for (const segment of ["Tests::BrowserApp", "::", typeName])
        for (const byte of new TextEncoder().encode(segment)) {
            value ^= BigInt(byte);
            value = BigInt.asUintN(64, value * 1099511628211n);
        }
    let index = BigInt(fieldIndex);
    for (let byte = 0; byte < 8; ++byte) {
        value ^= (index >> BigInt(byte * 8)) & 255n;
        value = BigInt.asUintN(64, value * 1099511628211n);
    }
    if (value === 0n) value = 1n;
    return value.toString(16).padStart(16, "0");
}

const project = instance.exports.aster_export_IncreaseProjected;
const batchData = instance.exports.aster_export_projection_batch_data;
const batchLength = instance.exports.aster_export_projection_batch_length;
const batchCount = instance.exports.aster_export_projection_batch_count;
const batchDrop = instance.exports.aster_export_projection_batch_drop;
if (typeof project !== "function" || typeof batchData !== "function" ||
    typeof batchDrop !== "function")
    throw new Error("compiled projection batch exports are incomplete");
const batch = Number(project(0n));
try {
    const pointer = Number(batchData(batch));
    const length = Number(batchLength(batch));
    const count = Number(batchCount(batch));
    const view = new DataView(memory.buffer, pointer, length);
    const bytes = new Uint8Array(memory.buffer, pointer, length);
    const records = new Map();
    let offset = 0;
    for (let record = 0; record < count; ++record) {
        const type = String.fromCharCode(bytes[offset]);
        const nameLength = bytes[offset + 1];
        const payloadLength = view.getUint32(offset + 4, true);
        offset += 8;
        const name = new TextDecoder().decode(
            bytes.subarray(offset, offset + nameLength)
        );
        offset += nameLength;
        let value;
        if (type === "b") value = bytes[offset] !== 0;
        else if (type === "l") value = view.getBigInt64(offset, true);
        else value = new TextDecoder().decode(
            bytes.subarray(offset, offset + payloadLength)
        );
        offset += payloadLength;
        records.set(name, value);
    }
    const counterPart = (field) => projectionPart(
        "CounterProjectionState", field
    );
    if (offset !== length || records.get(counterPart(0)) !== 1n ||
        records.get(counterPart(1)) !== false ||
        records.get(counterPart(2)) !== "Projected count: 1" ||
        records.get(counterPart(3)) !== "positive" ||
        [...records.keys()].some((name) =>
            ["count", "disabled", "summary", "className"].includes(name)
        ))
        throw new Error("compiled projection batch contents are wrong");
} finally {
    batchDrop(batch);
}
if (Object.keys(instance.exports).some(
    (name) => name.startsWith("aster_export_IncreaseProjected_result_")))
    throw new Error("projection state leaked flat aggregate accessors");

const componentNew =
    instance.exports.aster_export_component_PersistentTodoList_new;
const componentDrop =
    instance.exports.aster_export_component_PersistentTodoList_drop;
const componentAppend =
    instance.exports.aster_export_PersistentTodoList_AppendTodo;
const renderHtml = instance.exports.aster_export_html_render;
if (typeof componentNew !== "function" ||
    typeof componentDrop !== "function" ||
    typeof componentAppend !== "function")
    throw new Error("persistent class component exports are incomplete");
const component = Number(componentNew());
try {
    for (const expected of ["persistent-3", "persistent-4"]) {
        const html = Number(componentAppend(component));
        const rendered = Number(renderHtml(html));
        try {
            const pointer = Number(stringData(rendered));
            const length = Number(stringLength(rendered));
            const text = new TextDecoder().decode(
                new Uint8Array(memory.buffer, pointer, length)
            );
            if (!text.includes(`data-aster-key=\"${expected}\"`))
                throw new Error(
                    `persistent component lost state before ${expected}`
                );
        } finally {
            stringDrop(rendered);
        }
    }
    const rename = instance.exports.aster_export_PersistentTodoList_RenameTodo;
    const input = new TextEncoder().encode("persistent-1");
    const inputPointer = Number(
        instance.exports.aster_export_memory_alloc(input.length)
    );
    new Uint8Array(memory.buffer, inputPointer, input.length).set(input);
    try {
        for (const expected of ["First persistent!", "First persistent!!"]) {
            const renamed = Number(rename(
                component, inputPointer, input.length
            ));
            try {
                const pointer = Number(batchData(renamed));
                const length = Number(batchLength(renamed));
                const bytes = new Uint8Array(memory.buffer, pointer, length);
                const view = new DataView(memory.buffer, pointer, length);
                const records = new Map();
                let offset = 0;
                for (let record = 0;
                     record < Number(batchCount(renamed)); ++record) {
                    const type = String.fromCharCode(bytes[offset]);
                    const nameLength = bytes[offset + 1];
                    const payloadLength = view.getUint32(offset + 4, true);
                    offset += 8;
                    const part = new TextDecoder().decode(
                        bytes.subarray(offset, offset + nameLength)
                    );
                    offset += nameLength;
                    const value = type === "b"
                        ? bytes[offset] !== 0
                        : new TextDecoder().decode(bytes.subarray(
                            offset, offset + payloadLength
                        ));
                    offset += payloadLength;
                    records.set(part, value);
                }
                const part = (field) => projectionPart(
                    "PersistentTodoTitleProjectionState", field
                );
                if (offset !== length || records.size !== 3 ||
                    records.get(part(0)) !== expected ||
                    records.get(part(1)) !== "renamed" ||
                    records.get(part(2)) !== true)
                    throw new Error("keyed item projection state was lost");
            } finally {
                batchDrop(renamed);
            }
        }
    } finally {
        instance.exports.aster_export_memory_free(inputPointer);
    }
} finally {
    componentDrop(component);
}

const counterNew = instance.exports.aster_export_component_IsolatedCounter_new;
const counterDrop = instance.exports.aster_export_component_IsolatedCounter_drop;
const counterIncrement = instance.exports.aster_export_IsolatedCounter_Increment;
const counterFail = instance.exports.aster_export_IsolatedCounter_Fail;
const exceptionPending = instance.exports.aster_export_exception_pending;
const exceptionTake = instance.exports.aster_export_exception_take;
const droppedCounters = instance.exports.aster_export_ReadDroppedCounters;
if ([counterNew, counterDrop, counterIncrement, counterFail,
     exceptionPending, exceptionTake, droppedCounters].some(
    (value) => typeof value !== "function"
)) throw new Error("class component fault or disposal exports are incomplete");
const counter = Number(counterNew());
if (counterIncrement(counter, 0n) !== 1n ||
    counterIncrement(counter, 1n) !== 2n)
    throw new Error("class component instance state was not retained");
counterFail(counter, 2n);
if (exceptionPending() !== 1)
    throw new Error("browser handler exception was not published");
const exception = Number(exceptionTake());
try {
    const pointer = Number(stringData(exception));
    const length = Number(stringLength(exception));
    const message = new TextDecoder().decode(
        new Uint8Array(memory.buffer, pointer, length)
    );
    if (message !== "isolated component failure")
        throw new Error(`unexpected component failure: ${message}`);
} finally {
    stringDrop(exception);
}
if (exceptionPending() !== 0 || counterIncrement(counter, 2n) !== 13n)
    throw new Error("class component did not survive its handler fault");
counterDrop(counter);
if (droppedCounters(0n) !== 1n)
    throw new Error("class component destructor did not run exactly once");

const failingNew = instance.exports
    .aster_export_component_FailingConstructorComponent_new;
const constructionAttempts =
    instance.exports.aster_export_ReadConstructionAttempts;
for (let attempt = 1n; attempt <= 2n; ++attempt) {
    const failedHandle = Number(failingNew());
    if (failedHandle !== 0 || exceptionPending() !== 1)
        throw new Error("failed component construction leaked a handle");
    const failure = Number(exceptionTake());
    try {
        const pointer = Number(stringData(failure));
        const length = Number(stringLength(failure));
        const message = new TextDecoder().decode(
            new Uint8Array(memory.buffer, pointer, length)
        );
        if (message !== "component construction failure")
            throw new Error(`unexpected constructor failure: ${message}`);
    } finally {
        stringDrop(failure);
    }
    if (constructionAttempts(0n) !== attempt)
        throw new Error("failed component construction was not retried");
}

console.log("Lime browser keyed projections and class ownership verified");
