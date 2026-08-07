#!/usr/bin/env python3
import functools
import statistics
import sys
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from playwright.sync_api import sync_playwright

if len(sys.argv) != 2:
    raise SystemExit("usage: benchmark.py DIST_DIRECTORY")

directory = Path(sys.argv[1]).resolve()
(directory / "benchmark-host.html").write_text(
    "<!doctype html><meta charset='utf-8'><title>mount host</title>",
    encoding="utf-8"
)

class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

handler = functools.partial(QuietHandler, directory=str(directory))
server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
base = f"http://127.0.0.1:{server.server_port}"

operation_script = """
([selector, expected]) => new Promise((resolve, reject) => {
    const rows = document.querySelector("#row-list");
    const start = performance.now();
    const complete = () => {
        if (typeof expected === "number")
            return rows.children.length === expected;
        if (expected === "updated")
            return document.querySelector("#row-0 td:nth-child(2)")
                ?.textContent.endsWith("!!!") ?? false;
        if (expected === "swapped")
            return rows.children[1]?.id === "row-998";
        return false;
    };
    const done = () => {
        if (complete()) {
            observer.disconnect();
            resolve(performance.now() - start);
        }
    };
    const observer = new MutationObserver(done);
    observer.observe(rows, {childList: true, subtree: true, characterData: true});
    document.querySelector(selector).click();
    done();
    setTimeout(() => reject(new Error(
        `operation timed out with ${rows.children.length} rows`
    )), 10000);
})
"""

try:
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(
            executable_path="/usr/bin/google-chrome",
            headless=True,
            args=["--no-sandbox"]
        )
        cases = [
            ("Aster", "aster.html", [
                "[name=createAction]", "[name=updateAction]",
                "[name=swapAction]", "[name=appendAction]",
                "#row-500 button", "[name=clearAction]"
            ]),
            ("Vue", "vue.html", [
                "#create", "#update", "#swap", "#append",
                "#row-500 button", "#clear"
            ])
        ]
        for label, page_name, selectors in cases:
            measurements = [[], [], [], [], [], []]
            startup = []
            hydration = []
            async_latest = []
            memory_after_cycles = None
            mount_memory_after_cycles = None
            wasm_memory = 0
            for run in range(7):
                page = browser.new_page()
                errors = []
                page.on("pageerror", lambda error: errors.append(str(error)))
                page.goto(f"{base}/{page_name}")
                page.wait_for_function(
                    "globalThis.benchmarkReady !== undefined && "
                    "document.querySelector('#row-list') !== null"
                )
                timing = page.evaluate("""() => {
                    const navigation = performance.getEntriesByType('navigation')[0];
                    return {
                        ready: globalThis.benchmarkReady,
                        dom: navigation?.domContentLoadedEventEnd ?? 0
                    };
                }""")
                startup.append(timing["ready"])
                hydration.append(max(0, timing["ready"] - timing["dom"]))
                expectations = [
                    1000, "updated", "swapped", 2000, 1999, 0
                ]
                for index, selector in enumerate(selectors):
                    measurements[index].append(page.evaluate(
                        operation_script, [selector, expectations[index]]
                    ))
                async_selectors = (
                    ["[name=asyncSlow]", "[name=asyncFast]"]
                    if label == "Aster" else ["#async-slow", "#async-fast"]
                )
                async_latest.append(page.evaluate("""([slow, fast]) =>
                    new Promise((resolve, reject) => {
                        const output = document.querySelector(
                            '[name="asyncStatus"]'
                        );
                        const start = performance.now();
                        document.querySelector(slow).click();
                        document.querySelector(fast).click();
                        const check = () => {
                            if (output.textContent === 'fast') {
                                observer.disconnect();
                                resolve(performance.now() - start);
                            }
                        };
                        const observer = new MutationObserver(check);
                        observer.observe(output, {
                            childList: true, characterData: true, subtree: true
                        });
                        check();
                        setTimeout(() => reject(new Error(
                            'async latest completion timed out'
                        )), 1000);
                    })
                """, async_selectors))
                page.wait_for_timeout(30)
                if page.locator('[name="asyncStatus"]').text_content() != "fast":
                    raise RuntimeError(f"{label} stale async result committed")
                if run == 6:
                    session = page.context.new_cdp_session(page)
                    session.send("Performance.enable")
                    session.send("HeapProfiler.collectGarbage")
                    before = session.send("Performance.getMetrics")
                    before_heap = next(
                        metric["value"] for metric in before["metrics"]
                        if metric["name"] == "JSHeapUsedSize"
                    )
                    for _ in range(3):
                        page.evaluate(operation_script, [selectors[0], 1000])
                        page.evaluate(operation_script, [selectors[5], 0])
                    session.send("HeapProfiler.collectGarbage")
                    after = session.send("Performance.getMetrics")
                    after_heap = next(
                        metric["value"] for metric in after["metrics"]
                        if metric["name"] == "JSHeapUsedSize"
                    )
                    memory_after_cycles = int(after_heap - before_heap)
                    if label == "Aster":
                        wasm_memory = page.evaluate(
                            "benchmarkAster.exports.memory.buffer.byteLength"
                        )
                    host = browser.new_page()
                    host.goto(f"{base}/benchmark-host.html")
                    host_session = host.context.new_cdp_session(host)
                    host_session.send("Performance.enable")
                    host_session.send("HeapProfiler.collectGarbage")
                    host_before = host_session.send("Performance.getMetrics")
                    host_before_heap = next(
                        metric["value"] for metric in host_before["metrics"]
                        if metric["name"] == "JSHeapUsedSize"
                    )
                    host.evaluate("""async ([url, cycles]) => {
                        for (let cycle = 0; cycle < cycles; ++cycle) {
                            const frame = document.createElement('iframe');
                            frame.src = url;
                            document.body.append(frame);
                            await new Promise((resolve, reject) => {
                                const deadline = performance.now() + 5000;
                                const check = () => {
                                    if (frame.contentWindow?.benchmarkReady !== undefined)
                                        resolve();
                                    else if (performance.now() > deadline)
                                        reject(new Error('mount cycle timed out'));
                                    else
                                        setTimeout(check, 1);
                                };
                                check();
                            });
                            frame.remove();
                            await new Promise((resolve) => setTimeout(resolve, 0));
                        }
                    }""", [f"{base}/{page_name}", 10])
                    host_session.send("HeapProfiler.collectGarbage")
                    host_after = host_session.send("Performance.getMetrics")
                    host_after_heap = next(
                        metric["value"] for metric in host_after["metrics"]
                        if metric["name"] == "JSHeapUsedSize"
                    )
                    mount_memory_after_cycles = int(
                        host_after_heap - host_before_heap
                    )
                    host.close()
                if errors:
                    raise RuntimeError(f"{label} browser errors: {errors}")
                page.close()
            medians = [statistics.median(values) for values in measurements]
            print(
                f"{label}: startup={statistics.median(startup):.2f}ms "
                f"hydrate={statistics.median(hydration):.2f}ms "
                f"create={medians[0]:.2f}ms "
                f"update={medians[1]:.2f}ms swap={medians[2]:.2f}ms "
                f"append={medians[3]:.2f}ms delete={medians[4]:.2f}ms "
                f"clear={medians[5]:.2f}ms "
                f"async_latest={statistics.median(async_latest):.2f}ms "
                f"heap_delta_3_cycles={memory_after_cycles}B "
                f"mount_heap_delta_10={mount_memory_after_cycles}B "
                f"wasm_memory={wasm_memory}B"
            )
        browser.close()
finally:
    server.shutdown()
    server.server_close()
