import { readFile } from "node:fs/promises";

const directory = new URL("./dist/", import.meta.url);
const bytes = await readFile(new URL("counter.wasm", directory));
let memory;
const imports = {
    aster: {
        trap(pointer, length) {
            const view = new Uint8Array(memory.buffer, pointer, length);
            throw new Error(new TextDecoder().decode(view));
        }
    }
};
const { instance } = await WebAssembly.instantiate(bytes, imports);
memory = instance.exports.memory;

const increment = instance.exports.aster_export_Increment;
if (typeof increment !== "function") throw new Error("increment export missing");
if (increment(4n) !== 5n || increment(41n) !== 42n)
    throw new Error("Aster counter transition returned the wrong value");
const reset = instance.exports.aster_export_ResetCount;
const toggle = instance.exports.aster_export_ToggleDetails;
if (reset(41n) !== 0n || toggle(0) !== 1 || toggle(1) !== 0)
    throw new Error("Aster persistent-state transitions were incorrect");
const submit = instance.exports.aster_export_SubmitContact;
if (typeof submit !== "function") throw new Error("form export missing");
const encoder = new TextEncoder();
const decoder = new TextDecoder();
function withStrings(values, callback) {
    const arguments_ = [];
    const pointers = [];
    try {
        for (const value of values) {
            const encoded = encoder.encode(value);
            const pointer = Number(
                instance.exports.aster_export_memory_alloc(encoded.length)
            );
            new Uint8Array(memory.buffer, pointer, encoded.length).set(encoded);
            arguments_.push(pointer, encoded.length);
            pointers.push(pointer);
        }
        return callback(arguments_);
    } finally {
        for (const pointer of pointers)
            instance.exports.aster_export_memory_free(pointer);
    }
}
function takeString(handle) {
    try {
        const pointer = Number(instance.exports.aster_export_string_data(handle));
        const length = Number(instance.exports.aster_export_string_length(handle));
        return decoder.decode(new Uint8Array(memory.buffer, pointer, length));
    } finally {
        instance.exports.aster_export_string_drop(handle);
    }
}
for (let iteration = 0; iteration < 1000; ++iteration) {
    const message = withStrings(["Brändon", "hello@example.com"], (args) =>
        takeString(Number(submit(...args)))
    );
    if (message !== "Thanks, Brändon. We will reply to hello@example.com.")
        throw new Error("owned Aster form result was incorrect");
}
const rejected = withStrings(["B", "wrong"], (args) =>
    takeString(Number(submit(...args)))
);
if (rejected !== "")
    throw new Error("invalid Aster form input was accepted");

const addTodo = instance.exports.aster_export_AddTodo;
const todoNext = instance.exports.aster_export_AddTodo_result_l_nextId;
const todoItem = instance.exports.aster_export_AddTodo_result_h_item;
const todoDrop = instance.exports.aster_export_AddTodo_result_drop;
const renderHtml = instance.exports.aster_export_html_render;
for (let iteration = 0; iteration < 100; ++iteration) {
    const aggregate = withStrings(["A&B <native>"], (args) =>
        Number(addTodo(BigInt(iteration + 2), ...args))
    );
    try {
        if (todoNext(aggregate) !== BigInt(iteration + 3))
            throw new Error("aggregate scalar field was incorrect");
        const html = takeString(Number(renderHtml(Number(todoItem(aggregate)))));
        if (!html.includes(`id="todo-${iteration + 2}"`) ||
            !html.includes("A&amp;B &lt;native&gt;") ||
            !html.includes("click|RemoveTodo|r|s:removeKey"))
            throw new Error("aggregate native Html field was incorrect");
    } finally {
        todoDrop(aggregate);
    }
}

const replaceNotice = instance.exports.aster_export_ReplaceNotice;
if (typeof replaceNotice !== "function")
    throw new Error("direct Html export missing");
const notice = withStrings(["A&B <direct>"], (args) =>
    takeString(Number(renderHtml(Number(replaceNotice(...args)))))
);
if (!notice.includes('id="notice-current"') ||
    !notice.includes("A&amp;B &lt;direct&gt;"))
    throw new Error("direct owned Html result was incorrect");

const page = await readFile(new URL("index.html", directory), "utf8");
if (!page.includes('data-aster-event="click|Increment|l|l:count"') ||
    !page.includes('<output name="count">4</output>') ||
    !page.includes('data-aster-event="click|ResetCount|l|l:count"') ||
    !page.includes('data-aster-event="click|ToggleDetails|b|b:expanded"') ||
    !page.includes('data-aster-event="submit|AddTodo|a|l:nextId|s:title"') ||
    !page.includes('data-aster-event="submit|ReplaceNotice|h|s:notice"') ||
    !page.includes('data-aster-event="submit|SubmitContact|o|s:name|s:email"') ||
    !page.includes('id="contact-secondary"'))
    throw new Error("compiler-generated hydration metadata is missing");

console.log("wasm islands verified: typed exports, SSR counter, and two forms");
