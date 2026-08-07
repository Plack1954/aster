#!/usr/bin/env python3
import functools
import subprocess
import sys
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from playwright.sync_api import sync_playwright

if len(sys.argv) != 3:
    raise SystemExit("usage: final_todo_browser.py LANG OUTPUT_DIRECTORY")

lang = Path(sys.argv[1]).resolve()
directory = Path(sys.argv[2]).resolve()
page_html = subprocess.run(
    [str(lang), "project", "run", "packages/aster_web/FinalTodoProof.asproj"],
    check=True, capture_output=True, text=True
).stdout
page_html += """
<script type="module">
import {disposeAsterRoot, hydrateAster} from './aster.js';
await hydrateAster({wasmUrl: './Aster.Web.FinalTodoProof.wasm'});
window.disposeAsterRoot = disposeAsterRoot;
window.asterReady = true;
</script>
"""
(directory / "final-todo.html").write_text(page_html, encoding="utf-8")

class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

handler = functools.partial(QuietHandler, directory=str(directory))
server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
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
        page.goto(f"http://127.0.0.1:{server.server_port}/final-todo.html")
        page.wait_for_function("window.asterReady === true")

        components = page.locator(".final-todo-list")
        assert components.count() == 2
        alpha = page.locator('.final-todo-list[data-region="alpha"]')
        beta = page.locator('.final-todo-list[data-region="beta"]')
        assert alpha.locator(":scope > h2").text_content() == "Server alpha"
        assert beta.locator(":scope > h2").text_content() == "Server beta"
        assert alpha.locator(":scope > ul > li").count() == 2
        assert beta.locator(":scope > ul > li").count() == 2

        alpha_first = alpha.locator('[data-aster-key="alpha-10"]')
        alpha_input = alpha_first.locator('input')
        alpha_badge = alpha_first.locator('.todo-badge')
        alpha_badge.get_by_text("Touch badge", exact=True).click()
        assert alpha_badge.locator('[name="touches"]').text_content() == "1"
        page.evaluate("""window.finalTodoIdentity = [
            document.querySelector('[data-aster-key="alpha-10"]'),
            document.querySelector('[data-aster-key="alpha-10"] input'),
            document.querySelector('[data-aster-key="alpha-10"] .todo-badge')
        ]""")
        alpha_input.fill("browser edit")
        alpha_input.focus()
        alpha_input.evaluate("input => input.setSelectionRange(2, 7)")
        alpha_first.get_by_text("Rename", exact=True).evaluate(
            "button => button.click()"
        )
        assert alpha_first.locator(":scope > label > span").text_content() == (
            "Server alpha!"
        )
        assert alpha_input.input_value() == "browser edit"
        assert alpha_input.evaluate("input => document.activeElement === input")
        assert alpha_input.evaluate(
            "input => [input.selectionStart, input.selectionEnd]"
        ) == [2, 7]
        assert page.evaluate("window.finalTodoIdentity.every(node => node.isConnected)")
        assert alpha_badge.locator('[name="touches"]').text_content() == "1"

        alpha.get_by_text("Append", exact=True).click()
        page.wait_for_function(
            "document.querySelector('[data-aster-key=\"alpha-12\"]')"
        )
        assert alpha.locator(":scope > ul > li").count() == 3
        assert beta.locator(":scope > ul > li").count() == 2
        appended = alpha.locator('[data-aster-key="alpha-12"]')
        assert appended.get_by_text("Pending save", exact=True).is_visible()
        appended.get_by_text("Save", exact=True).click()
        page.wait_for_function("""document.querySelector(
            '[data-aster-key="alpha-12"]')?.className === 'saved'""")
        assert appended.get_by_text("Pending save", exact=True).is_hidden()
        assert appended.get_by_text("Saved", exact=True).is_visible()

        keys_before = alpha.locator(":scope > ul > li").evaluate_all(
            "rows => rows.map(row => row.dataset.asterKey)"
        )
        alpha.get_by_text("Reorder", exact=True).click()
        keys_after = alpha.locator(":scope > ul > li").evaluate_all(
            "rows => rows.map(row => row.dataset.asterKey)"
        )
        assert keys_after[:2] == [keys_before[1], keys_before[0]]
        assert page.evaluate("window.finalTodoIdentity.every(node => node.isConnected)")

        appended.get_by_text("Fail save", exact=True).click()
        page.wait_for_timeout(20)
        assert len(errors) == 1 and "save failed for alpha-12" in errors[0]
        errors.clear()
        appended.get_by_text("Remove", exact=True).click()
        page.wait_for_function(
            "!document.querySelector('[data-aster-key=\"alpha-12\"]')"
        )
        beta.get_by_text("Clear", exact=True).click()
        assert beta.locator(":scope > ul > li").count() == 0

        remaining_badge = alpha.locator(":scope > ul > li").nth(0).locator(
            ".todo-badge"
        )
        remaining_badge.get_by_text("Touch badge", exact=True).click()
        page.get_by_text("Read badge drops", exact=True).click()
        badge_drops_before = int(page.locator(
            '[name="todoBadgeDrops"]'
        ).text_content())
        alpha.locator(":scope > ul > li").nth(0).get_by_text(
            "Save", exact=True
        ).click()
        page.evaluate("window.disposeAsterRoot(document)")
        page.wait_for_timeout(30)
        page.get_by_text("Read todo drops", exact=True).click()
        page.get_by_text("Read badge drops", exact=True).click()
        assert page.locator('[name="finalTodoDrops"]').text_content() == "2"
        badge_drops_after = int(page.locator(
            '[name="todoBadgeDrops"]'
        ).text_content())
        assert badge_drops_after - badge_drops_before == 2
        page.evaluate("window.disposeAsterRoot(document)")
        assert errors == []
        browser.close()
finally:
    server.shutdown()
    server.server_close()

print("final server-loaded todo application proof verified")
