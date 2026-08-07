#!/usr/bin/env python3
import functools
import sys
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from playwright.sync_api import sync_playwright

if len(sys.argv) != 2:
    raise SystemExit("usage: aster_web_browser.py OUTPUT_DIRECTORY")

directory = Path(sys.argv[1]).resolve()

def projection_part(type_name, field_index):
    value = 14695981039346656037
    for segment in ("Tests::BrowserApp", "::", type_name):
        for byte in segment.encode():
            value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    for byte in int(field_index).to_bytes(8, "little"):
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    value = value or 1
    digits = "0123456789abcdefghijklmnopqrstuvwxyz"
    result = ""
    while value:
        result = digits[value % 36] + result
        value //= 36
    return result

projection_html = """<!doctype html>
<meta charset="utf-8">
<section id="native-keyed-list-trial">
  <ul id="native-keyed-list">
    <li data-aster-key="native-1">
      <label><!--a:{nativeTitle}-->Buy milk<!--/a:{nativeTitle}--> <input value="Buy milk"></label>
      <button type="button" name="key" value="native-1"
          aria-controls="native-keyed-list"
          data-aster-event="click|RemoveNativeTodo|h|s:key">Remove</button>
    </li>
    <li data-aster-key="native-2">
      <label><!--a:{nativeTitle}-->Walk dog<!--/a:{nativeTitle}--> <input value="Walk dog"></label>
      <button type="button" name="key" value="native-2"
          aria-controls="native-keyed-list"
          data-aster-event="click|RemoveNativeTodo|h|s:key">Remove</button>
    </li>
    <li data-aster-key="native-3">
      <label><!--a:{nativeTitle}-->Write Aster<!--/a:{nativeTitle}--> <input value="Write Aster"></label>
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
  <button type="button"
      data-aster-event="click|IsolatedCounter_FailRender|v|x:IsolatedCounter">Fail isolated render</button>
  <button type="button"
      data-aster-event="click|IsolatedCounter_RecoverRender|v|x:IsolatedCounter">Recover isolated render</button>
</section>
<section class="isolated-counter" data-aster-component="IsolatedCounter">
  <output name="count">0</output>
  <button type="button"
      data-aster-event="click|IsolatedCounter_Increment|l|x:IsolatedCounter|l:count">Increment isolated counter</button>
  <button type="button"
      data-aster-event="click|IsolatedCounter_Fail|l|x:IsolatedCounter|l:count">Fail isolated counter</button>
</section>
<section class="seeded-counter" data-aster-component="SeededCounter"
    data-aster-component-param-0="s" data-aster-component-arg-0="Alpha"
    data-aster-component-param-1="l" data-aster-component-arg-1="7"
    data-aster-component-param-2="b" data-aster-component-arg-2>
  <strong data-aster-part-t="{seededLabel}">Alpha</strong><output name="count" data-aster-part-t="{seededCount}">7</output>
  <button type="button"
      data-aster-event="click|SeededCounter_Increment|v|x:SeededCounter|l:count">Increment seeded counter</button>
  <aside class="nested-counter" data-aster-component="NestedCounter">
    <output name="count">0</output><button type="button"
        data-aster-event="click|NestedCounter_Increment|l|x:NestedCounter|l:count">Increment nested counter</button>
  </aside>
</section>
<section class="seeded-counter" data-aster-component="SeededCounter"
    data-aster-component-param-0="s" data-aster-component-arg-0="Beta"
    data-aster-component-param-1="l" data-aster-component-arg-1="40"
    data-aster-component-param-2="b">
  <strong data-aster-part-t="{seededLabel}">Beta</strong><output name="count" data-aster-part-t="{seededCount}">40</output>
  <button type="button"
      data-aster-event="click|SeededCounter_Increment|v|x:SeededCounter|l:count">Increment seeded counter</button>
  <aside class="nested-counter" data-aster-component="NestedCounter">
    <output name="count">0</output><button type="button"
        data-aster-event="click|NestedCounter_Increment|l|x:NestedCounter|l:count">Increment nested counter</button>
  </aside>
</section>
<section class="async-todo-component" data-aster-component="AsyncTodoComponent"
    data-aster-component-param-0="s" data-aster-component-arg-0="idle-one">
  <output name="asyncStatus" aria-label="idle-one" style="--accent: idle-one" data-aster-part-a="{asyncStatus}|aria-label" data-aster-part-s="{asyncStatus}|--accent"><strong>Status: </strong><!--a:{asyncStatus}-->idle-one<!--/a:{asyncStatus}--></output>
  <button type="button" data-aster-event="click|AsyncTodoComponent_SaveSlow|V|x:AsyncTodoComponent">Save slowly</button>
  <button type="button" data-aster-event="click|AsyncTodoComponent_SaveFast|V|x:AsyncTodoComponent">Save quickly</button>
  <button type="button" data-aster-event="click|AsyncTodoComponent_FailSave|V|x:AsyncTodoComponent">Fail save</button>
</section>
<section class="async-todo-component" data-aster-component="AsyncTodoComponent"
    data-aster-component-param-0="s" data-aster-component-arg-0="idle-two">
  <output name="asyncStatus" aria-label="idle-two" style="--accent: idle-two" data-aster-part-a="{asyncStatus}|aria-label" data-aster-part-s="{asyncStatus}|--accent"><strong>Status: </strong><!--a:{asyncStatus}-->idle-two<!--/a:{asyncStatus}--></output>
  <button type="button" data-aster-event="click|AsyncTodoComponent_SaveSlow|V|x:AsyncTodoComponent">Save slowly</button>
  <button type="button" data-aster-event="click|AsyncTodoComponent_SaveFast|V|x:AsyncTodoComponent">Save quickly</button>
  <button type="button" data-aster-event="click|AsyncTodoComponent_FailSave|V|x:AsyncTodoComponent">Fail save</button>
</section>
<section class="faulting-destructor-component" data-aster-component="FaultingDestructorComponent">
  <output name="destructorValue">0</output>
  <button type="button" data-aster-event="click|FaultingDestructorComponent_TouchDestructor|l|x:FaultingDestructorComponent|l:destructorValue">Touch destructor component</button>
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
  <output name="nestedDropCount">0</output>
  <button type="button"
      data-aster-event="click|ReadNestedDrops|l|l:nestedDropCount">Read nested drops</button>
  <output name="constructionAttempts">0</output>
  <button type="button"
      data-aster-event="click|ReadConstructionAttempts|l|l:constructionAttempts">Read construction attempts</button>
  <output name="asyncDropCount">0</output>
  <button type="button"
      data-aster-event="click|ReadAsyncComponentDrops|l|l:asyncDropCount">Read async component drops</button>
  <output name="destructorAttempts">0</output>
  <button type="button"
      data-aster-event="click|ReadDestructorAttempts|l|l:destructorAttempts">Read destructor attempts</button>
</section>
<section id="malformed-state-component" data-aster-component="PersistentTodoList"
    data-aster-component-list-state="k:{persistentKey},s:{persistentTitle},s:{persistentClass},b:{persistentDisabled},b:{persistentHidden},s:{persistentTooltip}">
  <ul id="malformed-state-list"><li data-aster-key="broken-state"></li></ul>
  <button type="button" aria-controls="malformed-state-list"
      data-aster-event="click|PersistentTodoList_AppendTodo|v|x:PersistentTodoList">Restore malformed state</button>
</section>
<section id="persistent-todo-component" data-aster-component="PersistentTodoList"
    data-aster-component-list-state="k:{persistentKey},s:{persistentTitle},s:{persistentClass},b:{persistentDisabled},b:{persistentHidden},s:{persistentTooltip}">
  <ul id="persistent-todo-list">
    <li data-aster-key="persistent-1" class="" title="First persistent todo"
        data-aster-part-c="{persistentClass}"
        data-aster-state-field-{persistentClass} data-aster-state-{persistentClass}=""
        data-aster-part-a="{persistentTooltip}"
        data-aster-state-field-{persistentTooltip} data-aster-state-{persistentTooltip}="First persistent todo">
      <label><span data-aster-part-t="{persistentTitle}" data-aster-state-field-{persistentTitle} data-aster-state-{persistentTitle}="First persistent">Todo: First persistent</span> <input value="First persistent" data-aster-part-d="{persistentDisabled}" data-aster-state-field-{persistentDisabled}><input type="checkbox"><small data-aster-part-h="{persistentHidden}" data-aster-state-field-{persistentHidden}>renamed detail</small></label>
      <aside class="nested-counter" data-aster-component="NestedCounter"><output name="count">0</output><button type="button" data-aster-event="click|NestedCounter_Increment|l|x:NestedCounter|l:count">Increment nested counter</button></aside>
      <button type="button" name="key" value="persistent-1"
          aria-controls="persistent-todo-list"
          data-aster-event="click|PersistentTodoList_RemoveTodo|v|x:PersistentTodoList|s:key">Remove</button>
      <button type="button" name="key" value="persistent-1"
          aria-controls="persistent-todo-list"
          data-aster-event="click|PersistentTodoList_RenameTodo|v|x:PersistentTodoList|s:key">Rename</button>
    </li>
    <li data-aster-key="persistent-2" class="" title="Server-loaded todo"
        data-aster-part-c="{persistentClass}"
        data-aster-state-field-{persistentClass} data-aster-state-{persistentClass}=""
        data-aster-part-a="{persistentTooltip}"
        data-aster-state-field-{persistentTooltip} data-aster-state-{persistentTooltip}="Server-loaded todo">
      <label><span data-aster-part-t="{persistentTitle}" data-aster-state-field-{persistentTitle} data-aster-state-{persistentTitle}="Server loaded">Todo: Server loaded</span> <input value="Server loaded" data-aster-part-d="{persistentDisabled}" data-aster-state-field-{persistentDisabled}><input type="checkbox"><small data-aster-part-h="{persistentHidden}" data-aster-state-field-{persistentHidden}>renamed detail</small></label>
      <aside class="nested-counter" data-aster-component="NestedCounter"><output name="count">0</output><button type="button" data-aster-event="click|NestedCounter_Increment|l|x:NestedCounter|l:count">Increment nested counter</button></aside>
      <button type="button" name="key" value="persistent-2"
          aria-controls="persistent-todo-list"
          data-aster-event="click|PersistentTodoList_RemoveTodo|v|x:PersistentTodoList|s:key">Remove</button>
      <button type="button" name="key" value="persistent-2"
          aria-controls="persistent-todo-list"
          data-aster-event="click|PersistentTodoList_RenameTodo|v|x:PersistentTodoList|s:key">Rename</button>
    </li>
  </ul>
  <button type="button" aria-controls="persistent-todo-list"
      data-aster-event="click|PersistentTodoList_AppendTodo|v|x:PersistentTodoList">Append persistent todo</button>
  <button type="button" aria-controls="persistent-todo-list"
      data-aster-event="click|PersistentTodoList_ClearTodos|v|x:PersistentTodoList">Clear persistent todos</button>
</section>
<script type="module">
import {disposeAsterRoot, hydrateAster} from "./aster.js";
await hydrateAster({wasmUrl: "./browser_http_server.wasm"});
window.disposeAsterRoot = disposeAsterRoot;
window.asterReady = true;
</script>
"""
projection_html = projection_html.replace(
    "{seededLabel}", projection_part("SeededCounter", 0)
)
projection_html = projection_html.replace(
    "{seededCount}", projection_part("SeededCounter", 3)
)
projection_html = projection_html.replace(
    "{asyncStatus}", projection_part("AsyncTodoComponent", 0)
)
projection_html = projection_html.replace(
    "{nativeTitle}", projection_part("NativeTodo", 1)
)
for field_index, field_name in (
    (0, "persistentKey"),
    (1, "persistentTitle"),
    (2, "persistentClass"),
    (3, "persistentDisabled"),
    (4, "persistentHidden"),
    (5, "persistentTooltip")
):
    projection_html = projection_html.replace(
        "{" + field_name + "}",
        projection_part("PersistentTodo", field_index)
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
        keyed = page.locator("#native-keyed-list")
        retained_input = keyed.locator(
            '[data-aster-key="native-1"] input'
        )
        retained_input.fill("browser-owned edit")
        retained_input.focus()
        retained_input.evaluate("input => input.setSelectionRange(2, 9)")
        page.evaluate("""window.nativeRetained = [
            document.querySelector('[data-aster-key="native-1"]'),
            document.querySelector('[data-aster-key="native-3"]'),
            document.querySelector('[data-aster-key="native-1"] input')
        ]""")
        page.get_by_text(
            "Append native todo", exact=True
        ).evaluate("button => button.click()")
        page.wait_for_function(
            "document.querySelector('[data-aster-key=\"native-4\"]')"
        )
        assert keyed.locator(":scope > li").count() == 4
        assert retained_input.evaluate("input => document.activeElement === input")
        assert retained_input.evaluate(
            "input => [input.selectionStart, input.selectionEnd]"
        ) == [2, 9]
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

        seeded = page.locator(".seeded-counter")
        assert seeded.count() == 2
        assert seeded.nth(0).locator("strong").text_content() == "Alpha"
        assert seeded.nth(1).locator("strong").text_content() == "Beta"
        nested = seeded.nth(0).locator(".nested-counter")
        page.evaluate("""window.nestedIdentity =
            document.querySelector('.seeded-counter .nested-counter')""")
        nested.get_by_text("Increment nested counter", exact=True).click()
        assert nested.locator('[name="count"]').text_content() == "1"
        seeded.nth(0).get_by_text(
            "Increment seeded counter", exact=True
        ).click()
        seeded.nth(1).get_by_text(
            "Increment seeded counter", exact=True
        ).click()
        assert seeded.nth(0).locator(':scope > [name="count"]').text_content() == "8"
        assert seeded.nth(1).locator(':scope > [name="count"]').text_content() == "42"
        assert nested.locator('[name="count"]').text_content() == "1"
        assert page.evaluate("window.nestedIdentity.isConnected")
        page.get_by_text("Read nested drops", exact=True).click()
        page.wait_for_function(
            "document.querySelector('[name=\"nestedDropCount\"]')"
            ".textContent === '2'"
        )
        seeded.nth(0).evaluate("component => component.remove()")
        page.wait_for_timeout(0)
        page.get_by_text("Read nested drops", exact=True).click()
        page.wait_for_function(
            "document.querySelector('[name=\"nestedDropCount\"]')"
            ".textContent === '3'"
        )
        assert seeded.count() == 1
        seeded.nth(0).get_by_text(
            "Increment seeded counter", exact=True
        ).click()
        assert seeded.nth(0).locator(
            ':scope > [name="count"]'
        ).text_content() == "44"

        async_components = page.locator(".async-todo-component")
        assert async_components.count() == 2
        first_async = async_components.nth(0)
        second_async = async_components.nth(1)
        page.evaluate("""window.asyncNestedIdentity = document.querySelector(
            '.async-todo-component [name="asyncStatus"] strong')""")
        first_async.get_by_text("Save slowly", exact=True).click()
        first_async.get_by_text("Save quickly", exact=True).click()
        page.wait_for_function("""document.querySelector(
            '.async-todo-component [name="asyncStatus"]'
        ).textContent === 'Status: fast-save'""")
        page.wait_for_timeout(50)
        assert first_async.locator(
            '[name="asyncStatus"]'
        ).text_content() == "Status: fast-save"
        assert first_async.locator('[name="asyncStatus"]').get_attribute(
            "aria-label"
        ) == "fast-save"
        assert first_async.locator('[name="asyncStatus"]').evaluate(
            "node => node.style.getPropertyValue('--accent').trim()"
        ) == "fast-save"
        assert page.evaluate("window.asyncNestedIdentity.isConnected")
        assert page.evaluate("""window.asyncNestedIdentity === document.querySelector(
            '.async-todo-component [name="asyncStatus"] strong')""")
        second_async.get_by_text("Fail save", exact=True).click()
        page.wait_for_timeout(20)
        assert len(errors) == 1 and "async component save failed" in errors[0]
        errors.clear()
        second_async.get_by_text("Save slowly", exact=True).click()
        second_async.evaluate("component => component.remove()")
        page.wait_for_timeout(60)
        page.get_by_text("Read async component drops", exact=True).click()
        page.wait_for_function(
            "document.querySelector('[name=\"asyncDropCount\"]')"
            ".textContent === '1'"
        )
        assert async_components.count() == 1
        assert first_async.locator(
            '[name="asyncStatus"]'
        ).text_content() == "Status: fast-save"
        assert errors == []

        faulting_destructor = page.locator(".faulting-destructor-component")
        faulting_destructor.get_by_text(
            "Touch destructor component", exact=True
        ).click()
        assert faulting_destructor.locator(
            '[name="destructorValue"]'
        ).text_content() == "1"
        faulting_destructor.evaluate("component => component.remove()")
        page.wait_for_timeout(20)
        assert len(errors) == 1 and "component destructor failure" in errors[0]
        errors.clear()
        page.get_by_text("Read destructor attempts", exact=True).click()
        assert page.locator(
            '[name="destructorAttempts"]'
        ).text_content() == "1"

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

        malformed_state = page.get_by_text(
            "Restore malformed state", exact=True
        )
        for _ in range(2):
            malformed_state.click()
            page.wait_for_timeout(20)
            assert len(errors) == 1 and (
                "component state field is missing" in errors[0]
            )
            errors.clear()

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
        first_counter.get_by_text("Fail isolated render", exact=True).click()
        page.wait_for_timeout(20)
        assert len(errors) == 1 and "isolated component render failure" in errors[0]
        errors.clear()
        assert first_counter.locator('[name="count"]').text_content() == "13"
        first_counter.get_by_text("Recover isolated render", exact=True).click()
        assert first_counter.locator('[name="count"]').text_content() == "13"
        assert second_counter.locator('[name="count"]').text_content() == "1"

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
            '[data-aster-key="persistent-1"] input:not([type="checkbox"])'
        )
        persistent_checkbox = persistent.locator(
            '[data-aster-key="persistent-1"] input[type="checkbox"]'
        )
        persistent_input.fill("persistent browser edit")
        persistent_checkbox.check()
        page.evaluate("""window.persistentIdentity = [
            document.querySelector('[data-aster-key="persistent-1"]'),
            document.querySelector('[data-aster-key="persistent-1"] input:not([type="checkbox"])')
        ]""")
        first_persistent = persistent.locator(
            '[data-aster-key="persistent-1"]'
        )
        second_persistent = persistent.locator(
            '[data-aster-key="persistent-2"]'
        )
        assert first_persistent.locator("[data-aster-project]").count() == 0
        first_persistent.get_by_text(
            "Rename", exact=True
        ).evaluate("button => button.click()")
        assert first_persistent.locator(
            "[data-aster-part-t]"
        ).text_content() == "Todo: First persistent!"
        assert first_persistent.get_attribute("class") == "renamed"
        assert first_persistent.get_attribute(
            "title"
        ) == "Renamed persistent todo"
        assert persistent_input.is_disabled()
        assert first_persistent.locator("small").is_hidden()
        assert second_persistent.locator(
            "[data-aster-part-t]"
        ).text_content() == "Todo: Server loaded"
        assert second_persistent.get_attribute("class") == ""
        assert second_persistent.get_attribute(
            "title"
        ) == "Server-loaded todo"
        assert not second_persistent.locator(
            'input:not([type="checkbox"])'
        ).is_disabled()
        assert not second_persistent.locator("small").is_hidden()
        assert persistent_input.input_value() == "persistent browser edit"
        assert persistent_checkbox.is_checked()
        assert page.evaluate("""window.persistentIdentity.every(
            (node) => node.isConnected
        )""")
        first_persistent.get_by_text("Rename", exact=True).click()
        assert first_persistent.locator(
            "[data-aster-part-t]"
        ).text_content() == "Todo: First persistent!!"

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
            "[data-aster-part-t]"
        ).text_content() == "Todo: Persistent 4!"
        assert fourth_persistent.get_attribute("class") == "renamed"
        assert fourth_persistent.get_attribute(
            "title"
        ) == "Renamed persistent todo"
        assert fourth_persistent.locator(
            'input:not([type="checkbox"])'
        ).is_disabled()
        assert fourth_persistent.locator("small").is_hidden()
        assert second_persistent.locator(
            "[data-aster-part-t]"
        ).text_content() == "Todo: Server loaded"
        assert persistent.locator(":scope > li").count() == 4
        persistent.locator(
            '[data-aster-key="persistent-3"]'
        ).get_by_text("Remove", exact=True).click()
        page.wait_for_function(
            "!document.querySelector('[data-aster-key=\"persistent-3\"]')"
        )
        assert persistent.locator(":scope > li").count() == 3
        assert persistent_input.input_value() == "persistent browser edit"
        first_persistent.get_by_text(
            "Increment nested counter", exact=True
        ).click()
        assert first_persistent.locator(
            ".nested-counter [name=\"count\"]"
        ).text_content() == "1"
        page.get_by_text("Read nested drops", exact=True).click()
        nested_before_keyed_remove = int(page.locator(
            '[name="nestedDropCount"]'
        ).text_content())
        first_persistent.get_by_text("Remove", exact=True).click()
        page.wait_for_function(
            "!document.querySelector('[data-aster-key=\"persistent-1\"]')"
        )
        page.wait_for_timeout(0)
        page.get_by_text("Read nested drops", exact=True).click()
        nested_after_keyed_remove = int(page.locator(
            '[name="nestedDropCount"]'
        ).text_content())
        assert nested_after_keyed_remove - nested_before_keyed_remove == 3
        page.locator("#persistent-todo-component").evaluate(
            "component => component.remove()"
        )
        page.wait_for_timeout(0)
        page.evaluate("window.disposeAsterRoot(document)")
        page.wait_for_timeout(0)
        page.get_by_text("Read async component drops", exact=True).click()
        page.wait_for_function(
            "document.querySelector('[name=\"asyncDropCount\"]')"
            ".textContent === '2'"
        )
        page.evaluate("window.disposeAsterRoot(document)")
        page.get_by_text("Read async component drops", exact=True).click()
        assert page.locator('[name="asyncDropCount"]').text_content() == "2"
        assert not errors, errors
        browser.close()
finally:
    server.shutdown()
    server.server_close()

print("component faults, exact disposal, and native retained state verified")
