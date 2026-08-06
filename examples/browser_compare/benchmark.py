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
            for _ in range(7):
                page = browser.new_page()
                errors = []
                page.on("pageerror", lambda error: errors.append(str(error)))
                page.goto(f"{base}/{page_name}")
                page.wait_for_function(
                    "document.querySelector('#row-list') !== null"
                )
                if label == "Aster":
                    page.wait_for_timeout(50)
                expectations = [
                    1000, "updated", "swapped", 2000, 1999, 0
                ]
                for index, selector in enumerate(selectors):
                    measurements[index].append(page.evaluate(
                        operation_script, [selector, expectations[index]]
                    ))
                if errors:
                    raise RuntimeError(f"{label} browser errors: {errors}")
                page.close()
            medians = [statistics.median(values) for values in measurements]
            print(
                f"{label}: create={medians[0]:.2f}ms "
                f"update={medians[1]:.2f}ms swap={medians[2]:.2f}ms "
                f"append={medians[3]:.2f}ms delete={medians[4]:.2f}ms "
                f"clear={medians[5]:.2f}ms"
            )
        browser.close()
finally:
    server.shutdown()
    server.server_close()
