#!/usr/bin/env python3
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread

from playwright.sync_api import sync_playwright


dist = Path(__file__).resolve().parent / "dist"
handler = partial(SimpleHTTPRequestHandler, directory=dist)
server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
thread = Thread(target=server.serve_forever, daemon=True)
thread.start()

try:
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(
            executable_path="/usr/bin/google-chrome",
            headless=True,
            args=["--no-sandbox"],
        )
        page = browser.new_page()
        page.goto(f"http://127.0.0.1:{server.server_port}/index.html")
        slot = page.locator('output[name="count"]')
        button = page.locator('button[data-aster-event^="click|Increment|"]')
        slot.wait_for(state="visible")
        assert slot.text_content() == "4"
        button.click()
        assert slot.text_content() == "5"
        slot.evaluate("node => { node.textContent = '999'; }")
        button.click()
        assert slot.text_content() == "6"
        reset = page.locator('button[data-aster-event^="click|ResetCount|"]')
        reset.click()
        assert slot.text_content() == "0"
        button.click()
        assert slot.text_content() == "1"
        disclosure = page.locator("#details-primary button")
        details = page.locator("#details-primary-panel")
        second_disclosure = page.locator("#details-secondary button")
        second_details = page.locator("#details-secondary-panel")
        assert disclosure.get_attribute("aria-expanded") == "false"
        assert details.is_hidden()
        disclosure.click()
        assert disclosure.get_attribute("aria-expanded") == "true"
        assert details.is_visible()
        todos = page.locator("#todo-island")
        todo_list = page.locator("#todo-list")
        todo_list.evaluate("node => { node.dataset.retained = 'yes'; }")
        assert todo_list.locator(":scope > li").count() == 2
        todos.locator('[name="title"]').fill("A&B <native>")
        todos.locator('button[type="submit"]').click()
        added = page.locator("#todo-2")
        assert added.locator("span").text_content() == "A&B <native>"
        assert todos.locator('[name="nextId"]').text_content() == "3"
        todos.locator('[name="nextId"]').evaluate(
            "node => { node.textContent = '999'; }"
        )
        todos.locator('[name="title"]').fill("Another item")
        todos.locator('button[type="submit"]').click()
        assert page.locator("#todo-3 span").text_content() == "Another item"
        assert todos.locator('[name="nextId"]').text_content() == "4"
        added.locator("button").click()
        assert page.locator("#todo-2").count() == 0
        page.locator("#todo-0 button").click()
        assert page.locator("#todo-0").count() == 0
        assert todo_list.get_attribute("data-retained") == "yes"
        for item_id in range(4, 54):
            todos.locator('[name="title"]').fill(f"Item {item_id}")
            todos.locator('button[type="submit"]').click()
            page.locator(f"#todo-{item_id} button").click()
        assert todos.locator('[name="nextId"]').text_content() == "54"
        assert page.locator("#todo-3").count() == 1
        notice = page.locator("#notice-island")
        notice_output = page.locator("#notice-output")
        notice_output.evaluate("node => { node.dataset.retained = 'yes'; }")
        notice.locator('[name="notice"]').fill("A&B <direct>")
        notice.locator('button[type="submit"]').click()
        assert page.locator("#notice-current").text_content() == "A&B <direct>"
        assert notice_output.get_attribute("data-retained") == "yes"
        assert second_disclosure.get_attribute("aria-expanded") == "false"
        assert second_details.is_hidden()
        disclosure.evaluate(
            "node => { node.setAttribute('aria-expanded', 'false'); }"
        )
        details.evaluate("node => { node.hidden = true; }")
        disclosure.click()
        assert disclosure.get_attribute("aria-expanded") == "false"
        assert details.is_hidden()
        disclosure.click()
        assert disclosure.get_attribute("aria-expanded") == "true"
        assert details.is_visible()
        primary = page.locator("#contact-primary")
        primary.locator('[name="name"]').fill("B")
        assert primary.locator("#contact-primary-name-error").is_visible()
        primary.locator('[name="name"]').fill("Brändon")
        assert primary.locator("#contact-primary-name-error").is_hidden()
        primary.locator('[name="email"]').fill("not-an-email")
        assert primary.locator("#contact-primary-email-error").is_visible()
        primary.locator('[name="email"]').fill("a@b.c")
        assert primary.locator("#contact-primary-email-error").is_hidden()
        primary.locator('button[type="submit"]').click()
        assert primary.locator("#contact-primary-success").is_visible()
        assert primary.locator("#contact-primary-success").text_content() == (
            "Thanks, Brändon. We will reply to a@b.c."
        )
        assert primary.locator("#contact-primary-error").is_hidden()
        secondary = page.locator("#contact-secondary")
        assert secondary.locator("#contact-secondary-success").is_hidden()
        for _ in range(100):
            primary.locator('button[type="submit"]').click()
        assert primary.locator("#contact-primary-success").text_content() == (
            "Thanks, Brändon. We will reply to a@b.c."
        )
        assert page.locator("form").count() == 4
        browser.close()
finally:
    server.shutdown()
    server.server_close()

print("browser islands verified: persistent aggregates and keyed updates")
