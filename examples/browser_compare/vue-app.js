import {createApp, h, ref} from "./vue.js";

createApp({
    setup() {
        const rows = ref([]);
        const updated = ref(new Set());
        let nextId = 0;
        const asyncStatus = ref("idle");
        let asyncVersion = 0;
        const completeAsync = async (delay, status) => {
            const version = ++asyncVersion;
            await new Promise((resolve) => setTimeout(resolve, delay));
            if (version === asyncVersion) asyncStatus.value = status;
        };
        const make = (count) => Array.from(
            {length: count}, () => nextId++
        );
        const remove = (id) => {
            rows.value = rows.value.filter((candidate) => candidate !== id);
        };
        return () => h("main", {id: "benchmark"}, [
            h("h1", "Vue keyed comparison"),
            h("button", {
                id: "create",
                onClick: () => {
                    nextId = 0;
                    updated.value = new Set();
                    rows.value = make(1000);
                }
            }, "Create 1,000"),
            h("button", {
                id: "append",
                onClick: () => {
                    rows.value = rows.value.concat(make(1000));
                }
            }, "Append 1,000"),
            h("button", {
                id: "update",
                onClick: () => {
                    const next = new Set(updated.value);
                    for (let id = 0; id < 1000; id += 10) next.add(id);
                    updated.value = next;
                }
            }, "Update every 10th"),
            h("button", {
                id: "swap",
                onClick: () => {
                    const next = rows.value.slice();
                    [next[1], next[998]] = [next[998], next[1]];
                    rows.value = next;
                }
            }, "Swap rows"),
            h("button", {
                id: "clear",
                onClick: () => { rows.value = []; }
            }, "Clear"),
            h("table", [h("tbody", {id: "row-list"},
                rows.value.map((id) => h("tr", {
                    id: `row-${id}`,
                    key: id
                }, [
                    h("td", String(id)),
                    h("td", `row ${id}${updated.value.has(id) ? " !!!" : ""}`),
                    h("td", [h("button", {
                        onClick: () => remove(id)
                    }, "Delete")])
                ]))
            )]),
            h("section", {id: "async-probe"}, [
                h("output", {name: "asyncStatus"}, asyncStatus.value),
                h("button", {
                    id: "async-slow",
                    onClick: () => completeAsync(25, "slow")
                }, "Slow async"),
                h("button", {
                    id: "async-fast",
                    onClick: () => completeAsync(1, "fast")
                }, "Fast async")
            ])
        ]);
    }
}).mount("#app");
globalThis.benchmarkReady = performance.now();
