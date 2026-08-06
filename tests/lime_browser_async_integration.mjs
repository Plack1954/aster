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

console.log("Lime browser async Task.Delay transition verified");
