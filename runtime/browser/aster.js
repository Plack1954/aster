const encoder = new TextEncoder();
const decoder = new TextDecoder();
const islandStates = new WeakMap();
const hydratedSources = new WeakSet();
const activeComponents = new Map();

function targetsFor(scope, name) {
    return scope.querySelectorAll(`[name="${CSS.escape(name)}"]`);
}

function targetFor(scope, name) {
    const named = targetsFor(scope, name)[0];
    if (named !== undefined) return named;
    for (const target of scope.querySelectorAll("[data-aster-project]")) {
        const [, field] = target.dataset.asterProject.split(":");
        if (field === name) return target;
    }
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

function stateKey(type, name) {
    return `${type}:${name}`;
}

function stateValue(source, scope, state, type, name) {
    if (type === "s") return targetValue(source, scope, type, name);
    const key = stateKey(type, name);
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
        const handle = Number(create());
        if (exports.aster_export_exception_pending() !== 0) {
            const message = takePendingException(exports, memory);
            throw new Error(message);
        }
        if (handle === 0)
            throw new Error(`Aster component construction failed: ${owner}`);
        component = {handle, drop, exports, memory};
        components.set(owner, component);
    }
    return component.handle;
}

function dropDisconnectedComponents() {
    for (const [scope, components] of activeComponents) {
        if (scope.isConnected) continue;
        for (const component of components.values()) {
            const {handle, drop, exports, memory} = component;
            drop(handle);
            if (exports.aster_export_exception_pending() !== 0)
                reportHandlerError(new Error(
                    takePendingException(exports, memory)
                ));
        }
        activeComponents.delete(scope);
        islandStates.delete(scope);
    }
}

function marshalArguments(source, scope, state, parameters, exports, memory) {
    const args = [];
    const allocations = [];
    for (const [type, name] of parameters) {
        if (type === "x") {
            args.push(componentInstance(scope, name, exports, memory));
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
    return {args, allocations};
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
    state.set(stateKey(resultType, name), value);
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

function validateAggregateProjections(source, scope, handlerName, fields) {
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
            `Aster projection target is missing: ${handlerName}.${name}`
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
        const next = new Map();
        for (const child of incoming) {
            const key = child.dataset.asterKey;
            const existing = collection.get(key);
            let retained = child;
            if (existing !== undefined && existing.isConnected)
                retained = existing;
            controlled.append(retained);
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

function removeControlledKey(source, state, key) {
    const controlled = controlledTarget(source);
    if (controlled === null) return false;
    const item = document.getElementById(key);
    if (item === null || item.parentElement !== controlled) return false;
    item.remove();
    const collection = state.get(`collection:${controlled.id}`);
    if (collection !== undefined) collection.delete(key);
    return true;
}

function clearControlledKeys(source, state) {
    const controlled = controlledTarget(source);
    if (controlled === null) return false;
    controlled.replaceChildren();
    const collection = state.get(`collection:${controlled.id}`);
    if (collection !== undefined) collection.clear();
    return true;
}

function swapControlledKeys(source, firstKey, secondKey) {
    const controlled = controlledTarget(source);
    if (controlled === null || firstKey === secondKey) return false;
    const first = document.getElementById(firstKey);
    const second = document.getElementById(secondKey);
    if (first === null || second === null ||
        first.parentElement !== controlled || second.parentElement !== controlled)
        return false;
    const marker = document.createTextNode("");
    first.replaceWith(marker);
    second.replaceWith(first);
    marker.replaceWith(second);
    return true;
}

function aggregateString(
    handle, fields, name, exports, memory
) {
    const field = fields.find(
        (candidate) => candidate.type === "o" && candidate.name === name
    );
    if (field === undefined)
        throw new Error(`Aster keyed operation field is missing: ${name}`);
    return decodeOwnedString(Number(field.accessor(handle)), exports, memory);
}

function applyProjectionBatch(
    handle, source, scope, state, exports, memory
) {
    if (handle === 0) throw new Error("Aster projection batch is null");
    try {
        const pointer = Number(
            exports.aster_export_projection_batch_data(handle)
        );
        const length = Number(
            exports.aster_export_projection_batch_length(handle)
        );
        const count = Number(
            exports.aster_export_projection_batch_count(handle)
        );
        const bytes = new Uint8Array(memory.buffer, pointer, length);
        const view = new DataView(memory.buffer, pointer, length);
        const records = [];
        let offset = 0;
        for (let record = 0; record < count; ++record) {
            if (offset + 8 > length)
                throw new Error("Aster projection batch header is truncated");
            const type = String.fromCharCode(bytes[offset]);
            const nameLength = bytes[offset + 1];
            const payloadLength = view.getUint32(offset + 4, true);
            offset += 8;
            if (offset + nameLength + payloadLength > length)
                throw new Error("Aster projection batch record is truncated");
            const name = decoder.decode(
                bytes.subarray(offset, offset + nameLength)
            );
            offset += nameLength;
            let value;
            if (type === "b") {
                if (payloadLength !== 1)
                    throw new Error("Aster Boolean projection is malformed");
                value = bytes[offset] !== 0;
            } else if (type === "l") {
                if (payloadLength !== 8)
                    throw new Error("Aster integer projection is malformed");
                value = view.getBigInt64(offset, true);
            } else if (type === "o" || type === "r") {
                value = decoder.decode(
                    bytes.subarray(offset, offset + payloadLength)
                );
            } else {
                throw new Error(`Aster projection type is unknown: ${type}`);
            }
            offset += payloadLength;
            records.push({type, name, value});
        }
        if (offset !== length)
            throw new Error("Aster projection batch has trailing data");

        const targets = [...scope.querySelectorAll("[data-aster-project]")];
        const recordsByName = new Map(
            records.filter((record) => record.type !== "r")
                .map((record) => [record.name, record])
        );
        for (const target of targets) {
            const descriptor = target.dataset.asterProject.split(":");
            if (descriptor.length !== 2)
                throw new Error("Aster projection marker is malformed");
            const [kind, field] = descriptor;
            const record = recordsByName.get(field);
            if (record === undefined)
                throw new Error(`Aster projection state field is missing: ${field}`);
            if ((kind === "d" && record.type !== "b") ||
                (kind === "c" && record.type !== "o") ||
                !["t", "d", "c"].includes(kind))
                throw new Error(`Aster projection type mismatch: ${kind}:${field}`);
        }
        const controlled = controlledTarget(source);
        const removalKeys = new Set();
        for (const {type, value} of records) {
            if (type !== "r") continue;
            const item = document.getElementById(value);
            if (controlled === null || item === null ||
                item.parentElement !== controlled || removalKeys.has(value))
                throw new Error(
                    `Aster projection cannot remove keyed item: ${value}`
                );
            removalKeys.add(value);
        }
        for (const {type, name, value} of records) {
            if (type === "r") {
                if (!removeControlledKey(source, state, value))
                    throw new Error(
                        `Aster projection could not remove keyed item: ${value}`
                    );
                continue;
            }
            state.set(stateKey(type, name), value);
            for (const target of targets) {
                const [kind, field] = target.dataset.asterProject.split(":");
                if (field !== name) continue;
                if (kind === "t")
                    target.textContent = String(value);
                else if (kind === "d")
                    target.disabled = Boolean(value);
                else if (kind === "c")
                    target.className = String(value);
                else
                    throw new Error(
                        `Aster projection kind is unknown: ${kind}`
                    );
            }
        }
    } finally {
        exports.aster_export_projection_batch_drop(handle);
    }
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
                state.set(stateKey(type, name), value);
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
    } else if (["a", "r", "c", "w"].includes(resultType)) {
        aggregateDrop(handle);
    } else if (resultType === "p") {
        exports.aster_export_projection_batch_drop(handle);
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

export async function hydrateAster({wasmUrl, root = document}) {
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
            const aggregateResult =
                ["a", "r", "c", "w"].includes(resultType);
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
            validateAggregateProjections(
                source, initialScope, handlerName, aggregateFields
            );
        if (["r", "c", "w"].includes(resultType) &&
            source.getAttribute("aria-controls") === null)
            throw new Error(
                `Aster keyed removal target is missing: ${handlerName}`
            );

        let transitionVersion = 0;
        source.addEventListener(eventName, async (event) => {
            const version = ++transitionVersion;
            const form = source.closest("form");
            const scope = eventScope(source, root);
            const state = stateFor(scope);
            let marshalled;
            let result;
            try {
                marshalled = marshalArguments(
                    source, scope, state, parameters,
                    instance.exports, memory
                );
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
                    if (version === transitionVersion) throw error;
                    return;
                } finally {
                    if (version === transitionVersion) {
                        source.removeAttribute("aria-busy");
                        if (disables) source.disabled = wasDisabled;
                    }
                }
                if (version !== transitionVersion) {
                    dropUnusedResult(
                        result, resultType, aggregateDrop, instance.exports
                    );
                    return;
                }
            }
            if (resultType === "p") {
                applyProjectionBatch(
                    Number(result), source, scope, state,
                    instance.exports, memory
                );
            } else if (resultType === "o") {
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
            } else if (resultType === "r") {
                if (typeof aggregateDrop !== "function")
                    throw new Error(
                        `Aster keyed removal drop export is missing: ${handlerName}`
                    );
                const handle = Number(result);
                try {
                    removeControlledKey(
                        source, state,
                        aggregateString(
                            handle, aggregateFields, "key",
                            instance.exports, memory
                        )
                    );
                } finally {
                    aggregateDrop(handle);
                }
            } else if (resultType === "c") {
                if (typeof aggregateDrop !== "function")
                    throw new Error(
                        `Aster keyed clear drop export is missing: ${handlerName}`
                    );
                const handle = Number(result);
                try {
                    clearControlledKeys(source, state);
                } finally {
                    aggregateDrop(handle);
                }
            } else if (resultType === "w") {
                if (typeof aggregateDrop !== "function")
                    throw new Error(
                        `Aster keyed swap drop export is missing: ${handlerName}`
                    );
                const handle = Number(result);
                try {
                    const first = aggregateString(
                        handle, aggregateFields, "first",
                        instance.exports, memory
                    );
                    const second = aggregateString(
                        handle, aggregateFields, "second",
                        instance.exports, memory
                    );
                    swapControlledKeys(source, first, second);
                } finally {
                    aggregateDrop(handle);
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
    }
    hydrateWithin(root);
    return instance;
}
