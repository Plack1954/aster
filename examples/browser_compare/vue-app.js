import {createApp, h, ref} from "./vue.js";

createApp({
    setup() {
        const rows = ref([]);
        const updated = ref(new Set());
        let nextId = 0;
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
            )])
        ]);
    }
}).mount("#app");
