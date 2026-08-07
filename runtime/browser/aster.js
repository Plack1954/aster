const encoder = new TextEncoder();
const decoder = new TextDecoder();
const islandStates = new WeakMap();
const hydratedSources = new WeakSet();
const activeComponents = new Map();
const hydrationRoots = new Map();

function targetsFor(scope, name) {
    return scope.querySelectorAll(`[name="${CSS.escape(name)}"]`);
}

function targetFor(scope, name) {
    const named = targetsFor(scope, name)[0];
    if (named !== undefined) return named;
    return null;
}

function targetValue(source, scope, type, name) {
    const selector = `[name="${CSS.escape(name)}"]`;
    const target = source.matches(selector) ? source : targetFor(scope, name);
    if (target === null)
        throw new Error(`Aster state target is missing: ${name}`);
    if (type === "s")
        return "value" in target ? target.value : target.textContent ?? "";
    let value;
    if (target instanceof HTMLInputElement ||
        target instanceof HTMLTextAreaElement ||
        target instanceof HTMLSelectElement) {
        if (target instanceof HTMLInputElement && target.type === "checkbox")
            value = target.checked ? 1 : 0;
        else if (target instanceof HTMLInputElement && target.type === "number")
            value = Number(target.value);
        else
            value = target.value.length;
    } else {
        value = Number.parseInt(target.textContent ?? "0", 10);
    }
    if (type === "b") return value === 0 ? 0 : 1;
    if (type === "l") return BigInt(value);
    return value;
}

function reportHandlerError(error) {
    if (typeof globalThis.reportError === "function")
        globalThis.reportError(error);
    else
        queueMicrotask(() => { throw error; });
}

function eventScope(source, root) {
    return source.closest("[data-aster-component]") ??
        source.closest("form") ?? source.closest("[id]") ?? root;
}

function stateFor(scope) {
    let state = islandStates.get(scope);
    if (state === undefined) {
        state = new Map();
        islandStates.set(scope, state);
    }
    return state;
}

function keyedSourceIdentity(source) {
    const item = source.closest("[data-aster-key]");
    if (item === null) return null;
    const collection = item.parentElement?.id ?? "";
    return `${collection}/${item.dataset.asterKey}`;
}

function stateKey(type, name, source) {
    const key = keyedSourceIdentity(source);
    return key === null ? `${type}:${name}` : `${type}:${name}@${key}`;
}

function stateValue(source, scope, state, type, name) {
    if (type === "s") return targetValue(source, scope, type, name);
    const key = stateKey(type, name, source);
    if (state.has(key)) return state.get(key);
    let value;
    const ariaName = `aria-${name.replaceAll("_", "-")}`;
    if (type === "b" && source.hasAttribute(ariaName))
        value = source.getAttribute(ariaName) === "true" ? 1 : 0;
    else
        value = targetValue(source, scope, type, name);
    state.set(key, value);
    return value;
}

function componentConstructorArguments(scope, exports, memory) {
    const args = [];
    const allocations = [];
    try {
        for (let index = 0; ; ++index) {
            const type = scope.getAttribute(
                `data-aster-component-param-${index}`
            );
            if (type === null) break;
            const name = `data-aster-component-arg-${index}`;
            if (type === "b") {
                args.push(scope.hasAttribute(name) ? 1 : 0);
            } else if (type === "l") {
                args.push(BigInt(scope.getAttribute(name) ?? "0"));
            } else if (type === "s") {
                const bytes = encoder.encode(scope.getAttribute(name) ?? "");
                const pointer = Number(
                    exports.aster_export_memory_alloc(bytes.length)
                );
                if (pointer === 0)
                    throw new Error("Aster component input allocation failed");
                new Uint8Array(memory.buffer, pointer, bytes.length).set(bytes);
                args.push(pointer, bytes.length);
                allocations.push(pointer);
            } else {
                throw new Error(
                    `Aster component parameter type is unknown: ${type}`
                );
            }
        }
        return {args, allocations};
    } catch (error) {
        for (const pointer of allocations)
            exports.aster_export_memory_free(pointer);
        throw error;
    }
}

function restoreComponentListState(scope, owner, handle, exports, memory) {
    const encoded = scope.getAttribute("data-aster-component-list-state");
    if (encoded === null) return;
    const clear = exports[`aster_export_component_${owner}_state_clear`];
    const add = exports[`aster_export_component_${owner}_state_add`];
    if (typeof clear !== "function" || typeof add !== "function")
        throw new Error(`Aster component list-state ABI is missing: ${owner}`);
    const fields = encoded.split(",").map((field) => {
        const separator = field.indexOf(":");
        return [field.slice(0, separator), field.slice(separator + 1)];
    });
    const items = [...scope.querySelectorAll("[data-aster-key]")].filter(
        (item) => item.closest("[data-aster-component]") === scope &&
            item.parentElement?.closest("[data-aster-key]") === null
    );
    clear(handle);
    for (const item of items) {
        const args = [handle];
        const allocations = [];
        try {
            for (const [type, id] of fields) {
                let value;
                if (type === "k") {
                    value = item.dataset.asterKey ?? "";
                } else {
                    const selector =
                        `[data-aster-state-field-${CSS.escape(id)}]`;
                    const target = item.matches(selector)
                        ? item : item.querySelector(selector);
                    if (target === null ||
                        target.closest("[data-aster-key]") !== item)
                        throw new Error(
                            `Aster component state field is missing: ${id}`
                        );
                    value = type === "b"
                        ? target.hasAttribute(`data-aster-state-${id}`)
                        : target.getAttribute(`data-aster-state-${id}`) ?? "";
                }
                if (type === "b") {
                    args.push(value ? 1 : 0);
                } else if (type === "l") {
                    args.push(BigInt(value));
                } else {
                    const bytes = encoder.encode(value);
                    const pointer = Number(
                        exports.aster_export_memory_alloc(bytes.length)
                    );
                    if (pointer === 0)
                        throw new Error("Aster component state allocation failed");
                    new Uint8Array(memory.buffer, pointer, bytes.length)
                        .set(bytes);
                    args.push(pointer, bytes.length);
                    allocations.push(pointer);
                }
            }
            add(...args);
        } finally {
            for (const pointer of allocations)
                exports.aster_export_memory_free(pointer);
        }
    }
}

function disposeComponent(component) {
    if (component.disposed) return;
    component.disposed = true;
    const {handle, drop, exports, memory} = component;
    try {
        drop(handle);
    } catch (error) {
        reportHandlerError(error);
    }
    if (exports.aster_export_exception_pending() !== 0)
        reportHandlerError(new Error(takePendingException(exports, memory)));
}

function retainComponent(component) {
    if (component.disposed || component.disconnected)
        throw new Error("Aster component is no longer active");
    component.pending += 1;
}

function releaseComponent(component) {
    if (component.pending === 0)
        throw new Error("Aster component pending-task lease underflow");
    component.pending -= 1;
    if (component.pending === 0 && component.disconnected)
        disposeComponent(component);
}

function componentInstance(scope, owner, exports, memory) {
    let components = activeComponents.get(scope);
    if (components === undefined) {
        components = new Map();
        activeComponents.set(scope, components);
    }
    let component = components.get(owner);
    if (component === undefined) {
        const create = exports[`aster_export_component_${owner}_new`];
        const drop = exports[`aster_export_component_${owner}_drop`];
        if (typeof create !== "function" || typeof drop !== "function")
            throw new Error(`Aster component ABI is missing: ${owner}`);
        const constructor = componentConstructorArguments(
            scope, exports, memory
        );
        let handle;
        try {
            handle = Number(create(...constructor.args));
        } finally {
            for (const pointer of constructor.allocations)
                exports.aster_export_memory_free(pointer);
        }
        if (exports.aster_export_exception_pending() !== 0) {
            const message = takePendingException(exports, memory);
            throw new Error(message);
        }
        if (handle === 0)
            throw new Error(`Aster component construction failed: ${owner}`);
        try {
            restoreComponentListState(scope, owner, handle, exports, memory);
        } catch (error) {
            drop(handle);
            if (exports.aster_export_exception_pending() !== 0)
                takePendingException(exports, memory);
            throw error;
        }
        component = {
            handle, drop, exports, memory, pending: 0,
            transitionVersion: 0, disconnected: false, disposed: false
        };
        components.set(owner, component);
    }
    if (component.disposed || component.disconnected)
        throw new Error(`Aster component is no longer active: ${owner}`);
    return component;
}

function detachComponentScope(scope, components) {
    for (const component of components.values()) {
        component.disconnected = true;
        component.transitionVersion += 1;
        if (component.pending === 0) disposeComponent(component);
    }
    activeComponents.delete(scope);
    islandStates.delete(scope);
}

function dropDisconnectedComponents() {
    for (const [scope, components] of activeComponents) {
        if (scope.isConnected) continue;
        detachComponentScope(scope, components);
    }
}

function marshalArguments(source, scope, state, parameters, exports, memory) {
    const args = [];
    const allocations = [];
    const components = [];
    for (const [type, name] of parameters) {
        if (type === "x") {
            const component = componentInstance(scope, name, exports, memory);
            args.push(component.handle);
            components.push(component);
            continue;
        }
        const value = stateValue(source, scope, state, type, name);
        if (type !== "s") {
            args.push(value);
            continue;
        }
        const bytes = encoder.encode(value);
        const pointer = Number(exports.aster_export_memory_alloc(bytes.length));
        if (pointer === 0) throw new Error("Aster Wasm input allocation failed");
        new Uint8Array(memory.buffer, pointer, bytes.length).set(bytes);
        args.push(pointer, bytes.length);
        allocations.push(pointer);
    }
    return {args, allocations, components};
}

function takePendingException(exports, memory) {
    const handle = Number(exports.aster_export_exception_take());
    return decodeOwnedString(handle, exports, memory);
}

function decodeOwnedString(handle, exports, memory) {
    if (handle === 0) throw new Error("Aster returned a null String");
    try {
        const pointer = Number(exports.aster_export_string_data(handle));
        const length = Number(exports.aster_export_string_length(handle));
        return decoder.decode(new Uint8Array(memory.buffer, pointer, length));
    } finally {
        exports.aster_export_string_drop(handle);
    }
}

function updateText(scope, name, value) {
    for (const target of targetsFor(scope, name)) {
        if (target instanceof HTMLInputElement ||
            target instanceof HTMLTextAreaElement ||
            target instanceof HTMLSelectElement)
            target.value = String(value);
        else
            target.textContent = String(value);
    }
}

function updateValidity(source, valid) {
    source.setAttribute("aria-invalid", valid ? "false" : "true");
    const describedBy = source.getAttribute("aria-describedby");
    if (describedBy === null) return;
    for (const id of describedBy.split(/\s+/)) {
        const message = document.getElementById(id);
        if (message !== null) message.hidden = valid;
    }
}

function updateBooleanState(source, scope, name, value) {
    for (const target of targetsFor(scope, name)) {
        if (target instanceof HTMLInputElement && target.type === "checkbox")
            target.checked = value;
        else if (target instanceof HTMLButtonElement)
            target.disabled = !value;
        else
            target.hidden = !value;
    }
    const ariaName = `aria-${name.replaceAll("_", "-")}`;
    if (source.hasAttribute(ariaName))
        source.setAttribute(ariaName, value ? "true" : "false");
    const controlledId = source.getAttribute("aria-controls");
    if (controlledId === null) return;
    const controlled = document.getElementById(controlledId);
    if (controlled !== null) controlled.hidden = !value;
}

function commitScalarState(
    source, scope, state, resultType, parameters, result
) {
    const parameter = parameters.find(([type]) => type !== "x");
    if (parameter === undefined || parameter[0] !== resultType)
        return false;
    const [, name] = parameter;
    const value = resultType === "b" ? (result !== 0 ? 1 : 0) : result;
    state.set(stateKey(resultType, name, source), value);
    if (resultType === "b")
        updateBooleanState(source, scope, name, value !== 0);
    else
        updateText(scope, name, value);
    return true;
}

function controlledTarget(source) {
    const controlledId = source.getAttribute("aria-controls");
    return controlledId === null ? null : document.getElementById(controlledId);
}

function validateAggregateTargets(source, scope, handlerName, fields) {
    for (const {type, name} of fields) {
        const hasNamedTarget = targetsFor(scope, name).length !== 0;
        const ariaName = `aria-${name.replaceAll("_", "-")}`;
        const hasControlledTarget = controlledTarget(source) !== null;
        if (hasNamedTarget ||
            (type === "h" && hasControlledTarget) ||
            (type === "b" &&
             (source.hasAttribute(ariaName) || hasControlledTarget)))
            continue;
        throw new Error(
            `Aster aggregate target is missing: ${handlerName}.${name}`
        );
    }
}

function retainedKey(node) {
    const key = node.dataset.asterKey;
    if (key !== undefined) return key;
    return node.id === "" ? null : node.id;
}

function collectionFor(source, state) {
    const controlled = controlledTarget(source);
    if (controlled === null) return null;
    const key = `collection:${controlled.id}`;
    let collection = state.get(key);
    if (collection === undefined) {
        collection = new Map();
        for (const child of controlled.children) {
            const key = retainedKey(child);
            if (key !== null) {
                if (collection.has(key))
                    throw new Error(`Aster collection key is duplicated: ${key}`);
                collection.set(key, child);
            }
        }
        state.set(key, collection);
    }
    return {controlled, collection};
}

const keyedPartKinds = ["t", "c", "d", "h", "a", "s"];

function keyedPartElements(region) {
    const elements = [region, ...region.querySelectorAll(
        "[data-aster-part-t], [data-aster-part-c], [data-aster-part-d], " +
        "[data-aster-part-h], [data-aster-part-a], [data-aster-part-s]"
    )];
    const keyed = region.matches("[data-aster-key]") ? region : null;
    const component = region.matches("[data-aster-component]")
        ? region : region.closest("[data-aster-component]");
    return elements.filter((element) =>
        element.closest("[data-aster-component]") === component &&
        (keyed === null
            ? element.closest("[data-aster-key]") === null
            : element.closest("[data-aster-key]") === keyed)
    );
}

function partRangeOwnerMatches(comment, region) {
    const parent = comment.parentElement;
    if (parent === null) return false;
    const keyed = region.matches("[data-aster-key]") ? region : null;
    const component = region.matches("[data-aster-component]")
        ? region : region.closest("[data-aster-component]");
    return parent.closest("[data-aster-component]") === component &&
        (keyed === null
            ? parent.closest("[data-aster-key]") === null
            : parent.closest("[data-aster-key]") === keyed);
}

function partRanges(region) {
    const starts = new Map();
    const ranges = [];
    const walker = document.createTreeWalker(region, NodeFilter.SHOW_COMMENT);
    for (let comment = walker.nextNode(); comment !== null;
         comment = walker.nextNode()) {
        if (!partRangeOwnerMatches(comment, region)) continue;
        const closing = comment.data.startsWith("/a:");
        if (!closing && !comment.data.startsWith("a:")) continue;
        const id = comment.data.slice(closing ? 3 : 2);
        if (id === "") throw new Error("Aster range part ID is empty");
        if (!closing) {
            if (starts.has(id))
                throw new Error(`Aster range part start is duplicated: ${id}`);
            starts.set(id, comment);
            continue;
        }
        const start = starts.get(id);
        if (start === undefined || start.parentNode !== comment.parentNode)
            throw new Error(`Aster range part layout is incompatible: ${id}`);
        let text = "";
        for (let node = start.nextSibling; node !== comment;
             node = node?.nextSibling ?? null) {
            if (node === null)
                throw new Error(`Aster range part is unterminated: ${id}`);
            text += node.textContent ?? "";
        }
        starts.delete(id);
        ranges.push({id, start, end: comment, text});
    }
    if (starts.size !== 0)
        throw new Error(
            `Aster range part end is missing: ${starts.keys().next().value}`
        );
    return ranges;
}

function keyedPartPlan(retained, incoming) {
    const existing = new Map();
    for (const element of keyedPartElements(retained))
        for (const kind of keyedPartKinds) {
            const id = element.dataset[`asterPart${kind.toUpperCase()}`];
            if (id === undefined) continue;
            const identity = `${kind}:${id}`;
            let targets = existing.get(identity);
            if (targets === undefined) {
                targets = [];
                existing.set(identity, targets);
            }
            targets.push(element);
        }
    for (const range of partRanges(retained)) {
        const identity = `r:${range.id}`;
        let targets = existing.get(identity);
        if (targets === undefined) {
            targets = [];
            existing.set(identity, targets);
        }
        targets.push(range);
    }
    const updates = [];
    const incomingCounts = new Map();
    for (const element of keyedPartElements(incoming))
        for (const kind of keyedPartKinds) {
            const id = element.dataset[`asterPart${kind.toUpperCase()}`];
            if (id === undefined) continue;
            const identity = `${kind}:${id}`;
            const index = incomingCounts.get(identity) ?? 0;
            incomingCounts.set(identity, index + 1);
            const target = existing.get(identity)?.[index];
            if (target === undefined)
                throw new Error(`Aster retained keyed part is missing: ${identity}`);
            updates.push({kind, target, value: element});
        }
    for (const range of partRanges(incoming)) {
        const identity = `r:${range.id}`;
        const index = incomingCounts.get(identity) ?? 0;
        incomingCounts.set(identity, index + 1);
        const target = existing.get(identity)?.[index];
        if (target === undefined)
            throw new Error(`Aster retained range part is missing: ${identity}`);
        updates.push({kind: "r", target, value: range});
    }
    for (const [identity, targets] of existing)
        if ((incomingCounts.get(identity) ?? 0) !== targets.length)
            throw new Error(
                `Aster incoming keyed snapshot omitted retained part: ${identity}`
            );
    return updates;
}

function applyKeyedPartPlan(updates) {
    for (const {kind, target, value} of updates) {
        if (kind === "r") {
            while (target.start.nextSibling !== target.end)
                target.start.nextSibling.remove();
            if (value.text !== "")
                target.end.before(document.createTextNode(value.text));
        } else if (kind === "t") target.textContent = value.textContent;
        else if (kind === "c") target.className = value.className;
        else if (kind === "d") target.disabled = value.disabled;
        else if (kind === "h") target.hidden = value.hidden;
        else if (kind === "a") {
            const descriptor = value.dataset.asterPartA;
            const separator = descriptor.indexOf("|");
            if (separator < 0) {
                target.title = value.title;
            } else {
                const name = descriptor.slice(separator + 1);
                if (value.hasAttribute(name))
                    target.setAttribute(name, value.getAttribute(name));
                else
                    target.removeAttribute(name);
            }
        } else if (kind === "s") {
            const descriptor = value.dataset.asterPartS;
            const separator = descriptor.indexOf("|");
            if (separator < 0)
                throw new Error("Aster CSS part is missing its property name");
            const name = descriptor.slice(separator + 1);
            target.style.setProperty(name, value.style.getPropertyValue(name));
        }
    }
}

function applyKeyedHtml(source, state, html, hydrateWithin) {
    const destination = collectionFor(source, state);
    if (destination === null)
        throw new Error("Aster aggregate Html requires an aria-controls target");
    const {controlled, collection} = destination;
    const range = document.createRange();
    range.selectNodeContents(controlled);
    const fragment = range.createContextualFragment(html);
    const incoming = [...fragment.children];
    const keyedSnapshot = incoming.some(
        (child) => child.dataset.asterKey !== undefined
    ) || [...controlled.children].some(
        (child) => child.dataset.asterKey !== undefined
    );
    if (keyedSnapshot) {
        const incomingKeys = new Set();
        for (const child of incoming) {
            const key = child.dataset.asterKey;
            if (key === undefined || key === "" || incomingKeys.has(key))
                throw new Error("Aster keyed list contains a missing or duplicate key");
            incomingKeys.add(key);
        }
        const plans = [];
        for (const child of incoming) {
            const existing = collection.get(child.dataset.asterKey);
            if (existing !== undefined && existing.isConnected)
                plans.push(keyedPartPlan(existing, child));
        }
        for (const plan of plans) applyKeyedPartPlan(plan);
        const next = new Map();
        let cursor = controlled.firstElementChild;
        for (const child of incoming) {
            const key = child.dataset.asterKey;
            const existing = collection.get(key);
            let retained = child;
            if (existing !== undefined && existing.isConnected)
                retained = existing;
            if (retained === cursor) {
                cursor = cursor.nextElementSibling;
            } else {
                controlled.insertBefore(retained, cursor);
            }
            next.set(key, retained);
        }
        for (const [key, existing] of collection)
            if (!next.has(key) && existing.parentElement === controlled)
                existing.remove();
        collection.clear();
        for (const [key, child] of next) collection.set(key, child);
    } else {
        for (const child of incoming) {
            const key = retainedKey(child);
            const existing = key === null ? null : collection.get(key);
            if (existing === null || existing === undefined ||
                !existing.isConnected)
                controlled.append(child);
            else
                existing.replaceWith(child);
            if (key !== null) collection.set(key, child);
        }
    }
    hydrateWithin(controlled);
}

function applyComponentSnapshot(
    source, scope, state, owner, exports, memory, hydrateWithin
) {
    const controlled = controlledTarget(source);
    const component = componentInstance(scope, owner, exports, memory);
    const handle = component.handle;
    const render = exports[`aster_export_component_${owner}_render`];
    if (typeof render !== "function")
        throw new Error(`Aster component render ABI is missing: ${owner}`);
    const htmlHandle = Number(render(handle));
    if (exports.aster_export_exception_pending() !== 0) {
        if (htmlHandle !== 0)
            dropUnusedResult(htmlHandle, "h", null, exports);
        throw new Error(takePendingException(exports, memory));
    }
    const stringHandle = Number(exports.aster_export_html_render(htmlHandle));
    const html = decodeOwnedString(stringHandle, exports, memory);
    const range = document.createRange();
    range.selectNode(scope);
    const fragment = range.createContextualFragment(html);
    const renderedScope = fragment.querySelector(
        `[data-aster-component="${CSS.escape(owner)}"]`
    ) ?? fragment.firstElementChild;
    if (renderedScope === null)
        throw new Error(`Aster component render root is missing: ${owner}`);
    if (controlled === null) {
        if (renderedScope.tagName !== scope.tagName)
            throw new Error(`Aster component render root changed: ${owner}`);
        applyKeyedPartPlan(keyedPartPlan(scope, renderedScope));
        return;
    }
    const controlledId = controlled.id;
    const snapshot = renderedScope.id === controlledId
        ? renderedScope
        : renderedScope.querySelector(`#${CSS.escape(controlledId)}`);
    if (snapshot === null)
        throw new Error(
            `Aster component controlled snapshot is missing: ${controlledId}`
        );
    applyKeyedHtml(source, state, snapshot.innerHTML, hydrateWithin);
}

function applyAggregateResult(
    handle, fields, drop, source, scope, state, exports, memory,
    hydrateWithin
) {
    try {
        for (const {type, name, accessor} of fields) {
            if (type === "h") {
                const htmlHandle = Number(accessor(handle));
                const stringHandle = Number(
                    exports.aster_export_html_render(htmlHandle)
                );
                applyKeyedHtml(
                    source, state,
                    decodeOwnedString(stringHandle, exports, memory),
                    hydrateWithin
                );
            } else if (type === "o") {
                const value = decodeOwnedString(
                    Number(accessor(handle)), exports, memory
                );
                state.set(stateKey(type, name, source), value);
                updateText(scope, name, value);
            } else {
                const value = accessor(handle);
                commitScalarState(
                    source, scope, state, type, [[type, name]], value
                );
            }
        }
    } finally {
        drop(handle);
    }
}

async function awaitBrowserTask(
    task, resultAccessor, exports, memory
) {
    if (task === 0) throw new Error("Aster returned a null Task");
    try {
        for (;;) {
            const status = exports.aster_export_task_status(task);
            if (status === 0) {
                await new Promise((resolve) => setTimeout(resolve, 1));
                continue;
            }
            if (status === 1) return resultAccessor(task);
            const error = decodeOwnedString(
                Number(exports.aster_export_task_error(task)), exports, memory
            );
            throw new Error(error);
        }
    } finally {
        exports.aster_export_task_drop(task);
    }
}

function dropUnusedResult(result, resultType, aggregateDrop, exports) {
    const handle = Number(result);
    if (handle === 0) return;
    if (resultType === "o") {
        exports.aster_export_string_drop(handle);
    } else if (resultType === "h") {
        const rendered = Number(
            exports.aster_export_html_render(handle)
        );
        exports.aster_export_string_drop(rendered);
    } else if (resultType === "a") {
        aggregateDrop(handle);
    }
}

function updateSubmission(form, message) {
    const accepted = message.length !== 0;
    const success = document.getElementById(`${form.id}-success`);
    const error = document.getElementById(`${form.id}-error`);
    if (success !== null) {
        if (accepted) success.textContent = message;
        success.hidden = !accepted;
    }
    if (error !== null) error.hidden = accepted;
}

export function disposeAsterRoot(root = document) {
    const hydration = hydrationRoots.get(root);
    if (hydration !== undefined) {
        hydration.observer?.disconnect();
        hydrationRoots.delete(root);
    }
    for (const [scope, components] of [...activeComponents]) {
        const owned = root instanceof Document
            ? root.documentElement?.contains(scope) === true
            : scope === root || root.contains(scope);
        if (owned) detachComponentScope(scope, components);
    }
}

export async function hydrateAster({wasmUrl, root = document}) {
    if (hydrationRoots.has(root))
        throw new Error("Aster root is already hydrated");
    let memory;
    const imports = {
        aster: {
            trap(pointer, length) {
                const bytes = new Uint8Array(memory.buffer, pointer, length);
                throw new Error(decoder.decode(bytes));
            },
            now_ms() {
                return BigInt(Math.trunc(performance.now()));
            }
        }
    };
    const response = await fetch(wasmUrl);
    const {instance} = await WebAssembly.instantiateStreaming(response, imports);
    memory = instance.exports.memory;

    const bindingCache = new Map();
    function hydrateWithin(container) {
      for (const source of container.querySelectorAll("[data-aster-event]")) {
        if (hydratedSources.has(source)) continue;
        const encodedBinding = source.dataset.asterEvent;
        let binding = bindingCache.get(encodedBinding);
        if (binding === undefined) {
            const [eventName, handlerName, encodedResultType,
                ...parameterBindings] = encodedBinding.split("|");
            const asyncResult =
                encodedResultType !== encodedResultType.toLowerCase();
            const resultType = encodedResultType.toLowerCase();
            const handler = instance.exports[`aster_export_${handlerName}`];
            if (typeof handler !== "function")
                throw new Error(`Aster Wasm export is missing: ${handlerName}`);
            const parameters = parameterBindings.map((parameter) => {
                const separator = parameter.indexOf(":");
                return [
                    parameter.slice(0, separator),
                    parameter.slice(separator + 1)
                ];
            });
            const aggregatePrefix = `aster_export_${handlerName}_result_`;
            const aggregateResult = resultType === "a";
            const aggregateFields = aggregateResult
                ? Object.entries(instance.exports).flatMap(([name, accessor]) => {
                    if (!name.startsWith(aggregatePrefix) ||
                        name.endsWith("_drop"))
                        return [];
                    const descriptor = name.slice(aggregatePrefix.length);
                    return [{
                        type: descriptor[0],
                        name: descriptor.slice(2),
                        accessor
                    }];
                })
                : [];
            const aggregateDrop = aggregateResult
                ? instance.exports[`${aggregatePrefix}drop`]
                : null;
            const taskResult = asyncResult
                ? instance.exports[`aster_export_${handlerName}_task_result`]
                : null;
            if (asyncResult && typeof taskResult !== "function")
                throw new Error(
                    `Aster Task result export is missing: ${handlerName}`
                );
            binding = {
                eventName, handlerName, asyncResult, resultType, handler,
                parameters, aggregateFields, aggregateDrop, taskResult
            };
            bindingCache.set(encodedBinding, binding);
        }
        const {
            eventName, handlerName, asyncResult, resultType, handler,
            parameters, aggregateFields, aggregateDrop, taskResult
        } = binding;
        const initialScope = eventScope(source, root);
        if (resultType === "a")
            validateAggregateTargets(
                source, initialScope, handlerName, aggregateFields
            );
        let sourceTransitionVersion = 0;
        source.addEventListener(eventName, async (event) => {
            const form = source.closest("form");
            const scope = eventScope(source, root);
            const state = stateFor(scope);
            let marshalled;
            let result;
            let transitionComponent = null;
            let version = 0;
            let leaseHeld = false;
            let leaseReleased = false;
            const releaseLease = () => {
                if (!leaseHeld || leaseReleased || marshalled === undefined)
                    return;
                leaseReleased = true;
                for (const component of new Set(marshalled.components))
                    releaseComponent(component);
            };
            const transitionIsCurrent = () => transitionComponent === null
                ? version === sourceTransitionVersion && source.isConnected
                : version === transitionComponent.transitionVersion &&
                    !transitionComponent.disconnected &&
                    !transitionComponent.disposed && scope.isConnected;
            try {
                marshalled = marshalArguments(
                    source, scope, state, parameters,
                    instance.exports, memory
                );
                if (asyncResult) {
                    transitionComponent = marshalled.components[0] ?? null;
                    version = transitionComponent === null
                        ? ++sourceTransitionVersion
                        : ++transitionComponent.transitionVersion;
                    for (const component of new Set(marshalled.components))
                        retainComponent(component);
                    leaseHeld = true;
                } else {
                    version = ++sourceTransitionVersion;
                }
                try {
                    result = handler(...marshalled.args);
                } finally {
                    for (const pointer of marshalled.allocations)
                        instance.exports.aster_export_memory_free(pointer);
                }
                if (instance.exports.aster_export_exception_pending() !== 0) {
                    const message = takePendingException(
                        instance.exports, memory
                    );
                    dropUnusedResult(
                        result, resultType, aggregateDrop, instance.exports
                    );
                    throw new Error(message);
                }
            } catch (error) {
                releaseLease();
                reportHandlerError(error);
                return;
            }
            // Synchronous forms retain ordinary submission when the Aster
            // transition traps before returning. An asynchronous transition
            // must reserve the event before its first suspension.
            if (eventName === "submit") event.preventDefault();
            if (asyncResult) {
                source.setAttribute("aria-busy", "true");
                const disables = source instanceof HTMLButtonElement;
                const wasDisabled = disables ? source.disabled : false;
                if (disables) source.disabled = true;
                try {
                    result = await awaitBrowserTask(
                        Number(result), taskResult, instance.exports, memory
                    );
                } catch (error) {
                    if (transitionIsCurrent()) reportHandlerError(error);
                    releaseLease();
                    return;
                } finally {
                    if (transitionIsCurrent()) {
                        source.removeAttribute("aria-busy");
                        if (disables) source.disabled = wasDisabled;
                    }
                }
                if (!transitionIsCurrent()) {
                    dropUnusedResult(
                        result, resultType, aggregateDrop, instance.exports
                    );
                    releaseLease();
                    return;
                }
            }
            try {
            if (resultType === "o") {
                const message = decodeOwnedString(
                    Number(result), instance.exports, memory
                );
                if (eventName === "submit" && form !== null) {
                    updateSubmission(form, message);
                } else {
                    const parameter = parameters.find(
                        ([type]) => type !== "x"
                    );
                    if (parameter !== undefined && parameter[0] === "s") {
                        const [, name] = parameter;
                        updateText(scope, name, message);
                    }
                }
            } else if (resultType === "h") {
                const stringHandle = Number(
                    instance.exports.aster_export_html_render(
                        Number(result)
                    )
                );
                applyKeyedHtml(
                    source, state,
                    decodeOwnedString(
                        stringHandle, instance.exports, memory
                    ),
                    hydrateWithin
                );
            } else if (resultType === "a") {
                if (typeof aggregateDrop !== "function")
                    throw new Error(
                        `Aster aggregate drop export is missing: ${handlerName}`
                    );
                applyAggregateResult(
                    Number(result), aggregateFields, aggregateDrop,
                    source, scope, state, instance.exports, memory,
                    hydrateWithin
                );
            } else if (resultType === "v") {
                const receiver = parameters.find(([type]) => type === "x");
                if (receiver !== undefined) {
                    try {
                        applyComponentSnapshot(
                            source, scope, state, receiver[1],
                            instance.exports, memory, hydrateWithin
                        );
                    } catch (error) {
                        reportHandlerError(error);
                    }
                }
            } else if (resultType === "b") {
                const accepted = result !== 0;
                if (eventName === "submit" && form !== null)
                    updateSubmission(form, accepted ? "Accepted" : "");
                else if (!commitScalarState(
                    source, scope, state, resultType, parameters, result
                ))
                    updateValidity(source, accepted);
            } else if (resultType !== "v")
                commitScalarState(
                    source, scope, state, resultType, parameters, result
                );
            } catch (error) {
                reportHandlerError(error);
            } finally {
                releaseLease();
            }
        });
        hydratedSources.add(source);
      }
    }
    const observedRoot = root instanceof Document
        ? root.documentElement : root;
    if (observedRoot !== null) {
        const componentObserver = new MutationObserver(
            dropDisconnectedComponents
        );
        componentObserver.observe(observedRoot, {childList: true, subtree: true});
        hydrationRoots.set(root, {observer: componentObserver});
    } else {
        hydrationRoots.set(root, {observer: null});
    }
    try {
        hydrateWithin(root);
    } catch (error) {
        disposeAsterRoot(root);
        throw error;
    }
    return instance;
}
