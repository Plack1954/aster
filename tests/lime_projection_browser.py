#!/usr/bin/env python3
import functools
import sys
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from playwright.sync_api import sync_playwright

if len(sys.argv) != 2:
    raise SystemExit("usage: lime_projection_browser.py OUTPUT_DIRECTORY")

directory = Path(sys.argv[1]).resolve()
(directory / "projection.html").write_text("""<!doctype html>
<meta charset="utf-8">
<section id="compiled-row-transition">
  <output data-aster-project="t:selectedId">1</output>
  <output data-aster-project="t:selectedLabel">Selected row 1</output>
  <ul id="projection-row-list">
    <li id="projection-row-1" class="selected"
        data-aster-project="c:firstClass">First row</li>
    <li id="projection-row-2" class=""
        data-aster-project="c:secondClass">
      Second row <input id="projection-row-input" value="server value">
    </li>
    <li id="projection-row-3" class=""
        data-aster-project="c:thirdClass">Third row</li>
  </ul>
  <button type="button" aria-controls="projection-row-list"
      data-aster-event="click|SelectSecondAndRemoveFirst|p|l:selectedId">
    Select second and remove first
  </button>
</section>
<script type="module">
import {hydrateAster} from "./aster.js";
await hydrateAster({wasmUrl: "./browser_http_server.wasm"});
window.asterReady = true;
</script>
""", encoding="utf-8")

class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

handler = functools.partial(QuietHandler, directory=str(directory))
server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()

try:
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(
            executable_path="/usr/bin/google-chrome",
            headless=True,
            args=["--no-sandbox"],
        )
        page = browser.new_page()
        errors = []
        page.on("pageerror", lambda error: errors.append(str(error)))
        page.goto(
            f"http://127.0.0.1:{server.server_port}/projection.html"
        )
        page.wait_for_function("window.asterReady === true")
        page.locator("#projection-row-input").fill("preserved by browser")
        page.evaluate("""window.retainedRows = [
            document.querySelector('#projection-row-2'),
            document.querySelector('#projection-row-3'),
            document.querySelector('#projection-row-input')
        ]""")
        page.get_by_text("Select second and remove first", exact=True).click()
        page.wait_for_function("!document.querySelector('#projection-row-1')")
        assert page.locator(
            '[data-aster-project="t:selectedId"]'
        ).text_content().strip() == "2"
        assert page.locator(
            '[data-aster-project="t:selectedLabel"]'
        ).text_content().strip() == "Selected row 2"
        assert page.locator("#projection-row-2").get_attribute(
            "class"
        ) == "selected"
        assert page.locator("#projection-row-3").get_attribute("class") == ""
        assert page.locator("#projection-row-input").input_value() == (
            "preserved by browser"
        )
        assert page.evaluate("""window.retainedRows.every((node, index) =>
            node === [
                document.querySelector('#projection-row-2'),
                document.querySelector('#projection-row-3'),
                document.querySelector('#projection-row-input')
            ][index])""")
        assert not errors, errors
        browser.close()
finally:
    server.shutdown()
    server.server_close()

print("compiled projections composed with keyed removal and retained row identity")
