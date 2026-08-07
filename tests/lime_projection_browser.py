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

def projection_part(type_name, field_index):
    value = 14695981039346656037
    for segment in ("Tests::BrowserApp", "::", type_name):
        for byte in segment.encode():
            value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    for byte in int(field_index).to_bytes(8, "little"):
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f"{value or 1:016x}"

projection_html = """<!doctype html>
<meta charset="utf-8">
<section id="compiled-row-transition">
  <output data-aster-project="t:{selectedId}">1</output>
  <output data-aster-project="t:{selectedLabel}">Selected row 1</output>
  <ul id="projection-row-list">
    <li id="projection-row-1" class="selected"
        data-aster-project="c:{firstClass}">First row</li>
    <li id="projection-row-2" class=""
        data-aster-project="c:{secondClass}">
      Second row <input id="projection-row-input" value="server value">
    </li>
    <li id="projection-row-3" class=""
        data-aster-project="c:{thirdClass}">Third row</li>
  </ul>
  <button type="button" aria-controls="projection-row-list"
      data-aster-event="click|SelectSecondAndRemoveFirst|p|l:@{selectedId}">
    Select second and remove first
  </button>
</section>
<section id="native-keyed-list-trial">
  <ul id="native-keyed-list">
    <li data-aster-key="native-1">
      <label>Buy milk <input value="Buy milk"></label>
      <button type="button" name="key" value="native-1"
          aria-controls="native-keyed-list"
          data-aster-event="click|RemoveNativeTodo|h|s:key">Remove</button>
    </li>
    <li data-aster-key="native-2">
      <label>Walk dog <input value="Walk dog"></label>
      <button type="button" name="key" value="native-2"
          aria-controls="native-keyed-list"
          data-aster-event="click|RemoveNativeTodo|h|s:key">Remove</button>
    </li>
    <li data-aster-key="native-3">
      <label>Write Aster <input value="Write Aster"></label>
      <button type="button" name="key" value="native-3"
          aria-controls="native-keyed-list"
          data-aster-event="click|RemoveNativeTodo|h|s:key">Remove</button>
    </li>
  </ul>
  <button type="button" aria-controls="native-keyed-list"
      data-aster-event="click|AppendNativeTodo|h">Append native todo</button>
  <button type="button" aria-controls="native-keyed-list"
      data-aster-event="click|MoveNativeTodo|h">Move native todo</button>
  <button type="button" aria-controls="native-keyed-list"
      data-aster-event="click|ClearNativeTodos|h">Clear native todos</button>
</section>
<section class="isolated-counter" data-aster-component="IsolatedCounter">
  <output name="count">0</output>
  <button type="button"
      data-aster-event="click|IsolatedCounter_Increment|l|x:IsolatedCounter|l:count">Increment isolated counter</button>
  <button type="button"
      data-aster-event="click|IsolatedCounter_Fail|l|x:IsolatedCounter|l:count">Fail isolated counter</button>
</section>
<section class="isolated-counter" data-aster-component="IsolatedCounter">
  <output name="count">0</output>
  <button type="button"
      data-aster-event="click|IsolatedCounter_Increment|l|x:IsolatedCounter|l:count">Increment isolated counter</button>
  <button type="button"
      data-aster-event="click|IsolatedCounter_Fail|l|x:IsolatedCounter|l:count">Fail isolated counter</button>
</section>
<section id="failing-constructor-component"
    data-aster-component="FailingConstructorComponent">
  <output name="value">0</output>
  <button type="button"
      data-aster-event="click|FailingConstructorComponent_Touch|l|x:FailingConstructorComponent|l:value">Construct failing component</button>
</section>
<section id="component-drop-probe">
  <output name="dropCount">0</output>
  <button type="button"
      data-aster-event="click|ReadDroppedCounters|l|l:dropCount">Read dropped counters</button>
  <output name="constructionAttempts">0</output>
  <button type="button"
      data-aster-event="click|ReadConstructionAttempts|l|l:constructionAttempts">Read construction attempts</button>
</section>
<section id="persistent-todo-component" data-aster-component="PersistentTodoList">
  <ul id="persistent-todo-list">
    <li data-aster-key="persistent-1" class=""
        data-aster-project="c:{persistentClass}">
      <label><span data-aster-project="t:{persistentTitle}">First persistent</span> <input value="First persistent" data-aster-project="d:{persistentDisabled}"></label>
      <button type="button" name="key" value="persistent-1"
          aria-controls="persistent-todo-list"
          data-aster-event="click|PersistentTodoList_RemoveTodo|h|x:PersistentTodoList|s:key">Remove</button>
      <button type="button" name="key" value="persistent-1"
          data-aster-event="click|PersistentTodoList_RenameTodo|p|x:PersistentTodoList|s:key">Rename</button>
    </li>
    <li data-aster-key="persistent-2" class=""
        data-aster-project="c:{persistentClass}">
      <label><span data-aster-project="t:{persistentTitle}">Second persistent</span> <input value="Second persistent" data-aster-project="d:{persistentDisabled}"></label>
      <button type="button" name="key" value="persistent-2"
          aria-controls="persistent-todo-list"
          data-aster-event="click|PersistentTodoList_RemoveTodo|h|x:PersistentTodoList|s:key">Remove</button>
      <button type="button" name="key" value="persistent-2"
          data-aster-event="click|PersistentTodoList_RenameTodo|p|x:PersistentTodoList|s:key">Rename</button>
    </li>
  </ul>
  <button type="button" aria-controls="persistent-todo-list"
      data-aster-event="click|PersistentTodoList_AppendTodo|h|x:PersistentTodoList">Append persistent todo</button>
  <button type="button" aria-controls="persistent-todo-list"
      data-aster-event="click|PersistentTodoList_ClearTodos|h|x:PersistentTodoList">Clear persistent todos</button>
</section>
<script type="module">
import {hydrateAster} from "./aster.js";
await hydrateAster({wasmUrl: "./browser_http_server.wasm"});
window.asterReady = true;
</script>
"""
for field_index, field_name in enumerate((
    "selectedId", "selectedLabel", "firstClass", "secondClass", "thirdClass"
)):
    projection_html = projection_html.replace(
        "{" + field_name + "}",
        projection_part("RowSelectionProjectionState", field_index)
    )
for field_index, field_name in enumerate((
    "persistentTitle", "persistentClass", "persistentDisabled"
)):
    projection_html = projection_html.replace(
        "{" + field_name + "}",
        projection_part("PersistentTodoTitleProjectionState", field_index)
    )
(directory / "projection.html").write_text(
    projection_html, encoding="utf-8"
)

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
        selected_id_part = projection_part(
            "RowSelectionProjectionState", 0
        )
        selected_label_part = projection_part(
            "RowSelectionProjectionState", 1
        )
        assert page.locator(
            f'[data-aster-project="t:{selected_id_part}"]'
        ).text_content().strip() == "2"
        assert page.locator(
            f'[data-aster-project="t:{selected_label_part}"]'
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
        keyed = page.locator("#native-keyed-list")
        retained_input = keyed.locator(
            '[data-aster-key="native-1"] input'
        )
        retained_input.fill("browser-owned edit")
        page.evaluate("""window.nativeRetained = [
            document.querySelector('[data-aster-key="native-1"]'),
            document.querySelector('[data-aster-key="native-3"]'),
            document.querySelector('[data-aster-key="native-1"] input')
        ]""")
        page.get_by_text("Append native todo", exact=True).click()
        page.wait_for_function(
            "document.querySelector('[data-aster-key=\"native-4\"]')"
        )
        assert keyed.locator(":scope > li").count() == 4
        keyed.locator('[data-aster-key="native-2"] button').click()
        page.wait_for_function(
            "!document.querySelector('[data-aster-key=\"native-2\"]')"
        )
        assert keyed.locator(":scope > li").count() == 2
        page.get_by_text("Move native todo", exact=True).click()
        page.wait_for_function("""document.querySelector(
            '#native-keyed-list > li')?.dataset.asterKey === 'native-3'""")
        assert keyed.locator(":scope > li").count() == 3
        assert retained_input.input_value() == "browser-owned edit"
        assert page.evaluate("""window.nativeRetained.every((node, index) =>
            node === [
                document.querySelector('[data-aster-key="native-1"]'),
                document.querySelector('[data-aster-key="native-3"]'),
                document.querySelector('[data-aster-key="native-1"] input')
            ][index])""")
        page.get_by_text("Clear native todos", exact=True).click()
        page.wait_for_function(
            "document.querySelector('#native-keyed-list').children.length === 0"
        )

        failing_constructor = page.get_by_text(
            "Construct failing component", exact=True
        )
        for attempt in range(2):
            failing_constructor.click()
            page.wait_for_timeout(50)
            assert len(errors) == 1 and (
                "component construction failure" in errors[0]
            )
            errors.clear()
        page.get_by_text("Read construction attempts", exact=True).click()
        page.wait_for_function(
            "document.querySelector('[name=\"constructionAttempts\"]')"
            ".textContent === '2'"
        )

        counters = page.locator(".isolated-counter")
        first_counter = counters.nth(0)
        second_counter = counters.nth(1)
        first_counter.get_by_text(
            "Increment isolated counter", exact=True
        ).click()
        first_counter.get_by_text(
            "Increment isolated counter", exact=True
        ).click()
        second_counter.get_by_text(
            "Increment isolated counter", exact=True
        ).click()
        assert first_counter.locator('[name="count"]').text_content() == "2"
        assert second_counter.locator('[name="count"]').text_content() == "1"
        first_counter.get_by_text(
            "Fail isolated counter", exact=True
        ).click()
        page.wait_for_timeout(50)
        assert len(errors) == 1 and "isolated component failure" in errors[0]
        errors.clear()
        first_counter.get_by_text(
            "Increment isolated counter", exact=True
        ).click()
        assert first_counter.locator('[name="count"]').text_content() == "13"

        first_counter.evaluate("component => component.remove()")
        page.wait_for_timeout(0)
        drop_probe = page.get_by_text("Read dropped counters", exact=True)
        drop_probe.click()
        page.wait_for_function(
            "document.querySelector('[name=\"dropCount\"]').textContent === '1'"
        )
        assert counters.count() == 1
        remaining_counter = counters.nth(0)
        remaining_counter.get_by_text(
            "Increment isolated counter", exact=True
        ).click()
        assert remaining_counter.locator(
            '[name="count"]'
        ).text_content() == "2"
        remaining_counter.evaluate("component => component.remove()")
        page.wait_for_timeout(0)
        drop_probe.click()
        page.wait_for_function(
            "document.querySelector('[name=\"dropCount\"]').textContent === '2'"
        )

        persistent = page.locator("#persistent-todo-list")
        persistent_input = persistent.locator(
            '[data-aster-key="persistent-1"] input'
        )
        persistent_input.fill("persistent browser edit")
        page.evaluate("""window.persistentIdentity = [
            document.querySelector('[data-aster-key="persistent-1"]'),
            document.querySelector('[data-aster-key="persistent-1"] input')
        ]""")
        first_persistent = persistent.locator(
            '[data-aster-key="persistent-1"]'
        )
        second_persistent = persistent.locator(
            '[data-aster-key="persistent-2"]'
        )
        first_persistent.get_by_text("Rename", exact=True).click()
        assert first_persistent.locator(
            '[data-aster-project^="t:"]'
        ).text_content() == "First persistent!"
        assert first_persistent.get_attribute("class") == "renamed"
        assert persistent_input.is_disabled()
        assert second_persistent.locator(
            '[data-aster-project^="t:"]'
        ).text_content() == "Second persistent"
        assert second_persistent.get_attribute("class") == ""
        assert not second_persistent.locator("input").is_disabled()
        assert persistent_input.input_value() == "persistent browser edit"
        assert page.evaluate("""window.persistentIdentity.every(
            (node) => node.isConnected
        )""")
        first_persistent.get_by_text("Rename", exact=True).click()
        assert first_persistent.locator(
            '[data-aster-project^="t:"]'
        ).text_content() == "First persistent!!"

        persistent_append = page.get_by_text(
            "Append persistent todo", exact=True
        )
        persistent_append.click()
        page.wait_for_function(
            "document.querySelector('[data-aster-key=\"persistent-3\"]')"
        )
        persistent_append.click()
        page.wait_for_function(
            "document.querySelector('[data-aster-key=\"persistent-4\"]')"
        )
        fourth_persistent = persistent.locator(
            '[data-aster-key="persistent-4"]'
        )
        fourth_persistent.get_by_text("Rename", exact=True).click()
        assert fourth_persistent.locator(
            '[data-aster-project^="t:"]'
        ).text_content() == "Persistent 4!"
        assert fourth_persistent.get_attribute("class") == "renamed"
        assert fourth_persistent.locator("input").is_disabled()
        assert second_persistent.locator(
            '[data-aster-project^="t:"]'
        ).text_content() == "Second persistent"
        assert persistent.locator(":scope > li").count() == 4
        persistent.locator(
            '[data-aster-key="persistent-3"]'
        ).get_by_text("Remove", exact=True).click()
        page.wait_for_function(
            "!document.querySelector('[data-aster-key=\"persistent-3\"]')"
        )
        assert persistent.locator(":scope > li").count() == 3
        assert persistent_input.input_value() == "persistent browser edit"
        page.locator("#persistent-todo-component").evaluate(
            "component => component.remove()"
        )
        page.wait_for_timeout(0)
        assert not errors, errors
        browser.close()
finally:
    server.shutdown()
    server.server_close()

print("keyed item projections, class ownership, and retained DOM verified")
