import {createApp, h, ref} from "./vue.js";

createApp({
    setup() {
        const rows = ref([]);
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
                    rows.value = make(1000);
                }
            }, "Create 1,000"),
            h("button", {
                id: "append",
                onClick: () => {
                    rows.value = rows.value.concat(make(1000));
                }
            }, "Append 1,000"),
            h("table", [h("tbody", {id: "row-list"},
                rows.value.map((id) => h("tr", {
                    id: `row-${id}`,
                    key: id
                }, [
                    h("td", String(id)),
                    h("td", `row ${id}`),
                    h("td", [h("button", {
                        onClick: () => remove(id)
                    }, "Delete")])
                ]))
            )])
        ]);
    }
}).mount("#app");
