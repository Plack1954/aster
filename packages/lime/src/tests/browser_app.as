namespace Tests.BrowserApp;

using Aster.Html;
using System.Text;

public struct TodoPatch
{
    int nextId;
    Html item;
}

public struct NativeTodo
{
    string key;
    string title;
}

private struct PersistentTodo
{
    string key;
    string title;
    string className;
    bool disabled;
    bool hidden;
    string tooltip;
}

private class PersistentTodoList
{
    private List<PersistentTodo> todos;
    private int nextId;

    public PersistentTodoList()
    {
        todos = new();
        todos.Add(new()
        {
            key = "persistent-1",
            title = "First persistent",
            className = "",
            disabled = false,
            hidden = false,
            tooltip = "First persistent todo"
        });
        todos.Add(new()
        {
            key = "persistent-2",
            title = "Second persistent",
            className = "",
            disabled = false,
            hidden = false,
            tooltip = "Second persistent todo"
        });
        nextId = 3;
    }

    private Html Rows()
    {
        List<Html> rows = new();
        foreach (PersistentTodo todo in todos)
        {
            rows.Add(
                <li
                    key=todo.key
                    class=todo.className
                    title=todo.tooltip
                >
                    <label>
                        <span>Todo: {todo.title}</span>
                        <input value=todo.title disabled=todo.disabled />
                        <input type="checkbox" checked=false />
                        <small hidden=todo.hidden>renamed detail</small>
                    </label>
                    <button
                        type="button"
                        name="key"
                        value=todo.key
                        aria-controls="persistent-todo-list"
                        onclick=this.RemoveTodo
                    >Remove</button>
                    <button
                        type="button"
                        name="key"
                        value=todo.key
                        aria-controls="persistent-todo-list"
                        onclick=this.RenameTodo
                    >Rename</button>
                </li>
            );
        }
        return <>{rows}</>;
    }

    private void AppendTodo()
    {
        string key = $"persistent-{this.nextId}";
        PersistentTodo todo = new()
        {
            key = key,
            title = $"Persistent {this.nextId}",
            className = "",
            disabled = false,
            hidden = false,
            tooltip = $"Persistent todo {this.nextId}"
        };
        this.todos.Add(todo);
        this.nextId += 1;
    }

    private void RemoveTodo(string key)
    {
        for (nuint index = 0; index < this.todos.Count; index++)
        {
            if (this.todos[index].key == key)
            {
                this.todos.RemoveAt(index);
                break;
            }
        }
    }

    private void RenameTodo(string key)
    {
        for (nuint index = 0; index < this.todos.Count; index++)
        {
            if (this.todos[index].key == key)
            {
                PersistentTodo todo = this.todos[index];
                todo.title = $"{todo.title}!";
                todo.className = "renamed";
                todo.disabled = true;
                todo.hidden = true;
                todo.tooltip = "Renamed persistent todo";
                this.todos.Set(index, todo);
                return;
            }
        }
    }

    private void ClearTodos()
    {
        this.todos.Clear();
    }

    public Html Render()
    {
        return <section id="persistent-todo-component">
            <h2>Persistent class component</h2>
            <ul id="persistent-todo-list">{this.Rows()}</ul>
            <button
                type="button"
                aria-controls="persistent-todo-list"
                onclick=this.AppendTodo
            >Append persistent todo</button>
            <button
                type="button"
                aria-controls="persistent-todo-list"
                onclick=this.ClearTodos
            >Clear persistent todos</button>
        </section>;
    }
}

private class IsolatedCounter
{
    private int count;
    private static int droppedCount;

    public IsolatedCounter()
    {
        this.count = 0;
    }

    ~IsolatedCounter()
    {
        droppedCount += 1;
    }

    public static int Dropped => droppedCount;

    private int Increment(int count)
    {
        this.count += 1;
        return this.count;
    }

    private int Fail(int count)
    {
        this.count += 10;
        throw new Exception("isolated component failure");
    }

    public Html Render()
    {
        return <section class="isolated-counter">
            <output name="count">0</output>
            <button type="button" onclick=this.Increment>
                Increment isolated counter
            </button>
            <button type="button" onclick=this.Fail>
                Fail isolated counter
            </button>
        </section>;
    }
}

private class NestedCounter
{
    private int count;
    private static int dropped;

    public NestedCounter()
    {
        this.count = 0;
    }

    ~NestedCounter()
    {
        dropped += 1;
    }

    public static int Dropped => dropped;

    private int Increment(int count)
    {
        this.count += 1;
        return this.count;
    }

    public Html Render()
    {
        return <aside class="nested-counter">
            <output name="count">0</output>
            <button type="button" onclick=this.Increment>
                Increment nested counter
            </button>
        </aside>;
    }
}

private class SeededCounter
{
    private string label;
    private int initial;
    private bool enabled;
    private int count;

    public SeededCounter(string label, int initial, bool enabled)
    {
        this.label = label;
        this.initial = initial;
        this.enabled = enabled;
        this.count = initial;
    }

    private void Increment(int count)
    {
        if (this.enabled)
        {
            this.count += 1;
        }
        else
        {
            this.count += 2;
        }
    }

    public Html Render()
    {
        return <section class="seeded-counter">
            <strong>{this.label}</strong>
            <output name="count">{this.count}</output>
            <button type="button" onclick=this.Increment>
                Increment seeded counter
            </button>
            <NestedCounter />
        </section>;
    }
}

public int ReadNestedDrops(int nestedDropCount)
{
    return NestedCounter.Dropped;
}

public int ReadDroppedCounters(int dropCount)
{
    return IsolatedCounter.Dropped;
}

private class FailingConstructorComponent
{
    private static int attempts;

    public FailingConstructorComponent()
    {
        attempts += 1;
        throw new Exception("component construction failure");
    }

    public static int Attempts => attempts;

    private int Touch(int value)
    {
        return value + 1;
    }

    public Html Render()
    {
        return <section>
            <output name="value">0</output>
            <button type="button" onclick=this.Touch>
                Construct failing component
            </button>
        </section>;
    }
}

public int ReadConstructionAttempts(int constructionAttempts)
{
    return FailingConstructorComponent.Attempts;
}

public struct ReactiveCounterPatch
{
    int value;
    int doubled;
    bool positive;
    bool canDecrease;
    string summary;
}

public struct QueryProjection
{
    nuint length;
    bool valid;
    string preview;
}

public struct CounterProjectionState
{
    int count;
    bool disabled;
    string summary;
    string className;
}

public struct RowSelectionProjectionState
{
    int selectedId;
    string selectedLabel;
    string firstClass;
    string secondClass;
    string thirdClass;
}

public struct RowSelectionProjectionTransition
{
    RowSelectionProjectionState state;
    KeyedRemove removal;
}

private extern Task Task.Delay(int milliseconds);

private class AsyncTodoComponent
{
    private string status;
    private static int dropped;

    public AsyncTodoComponent(string status)
    {
        this.status = status;
    }

    ~AsyncTodoComponent()
    {
        dropped += 1;
    }

    public static int Dropped => dropped;

    private async Task SaveSlow()
    {
        await Task.Delay(30);
        this.status = "slow-save";
    }

    private async Task SaveFast()
    {
        await Task.Delay(1);
        this.status = "fast-save";
    }

    private async Task FailSave()
    {
        await Task.Delay(1);
        throw new Exception("async component save failed");
    }

    public Html Render()
    {
        return <section class="async-todo-component">
            <output name="asyncStatus" aria-label=this.status --accent=this.status><strong>Status: </strong>{this.status}</output>
            <button type="button" onclick=this.SaveSlow>Save slowly</button>
            <button type="button" onclick=this.SaveFast>Save quickly</button>
            <button type="button" onclick=this.FailSave>Fail save</button>
        </section>;
    }
}

public int ReadAsyncComponentDrops(int asyncDropCount)
{
    return AsyncTodoComponent.Dropped;
}

public int Increment(int count)
{
    return count + 1;
}

public async Task<int> IncrementLater(int count)
{
    await Task.Delay(25);
    return count + 1;
}

public async Task<int> FailLater(int count)
{
    await Task.Delay(1);
    throw new Exception("browser async failure");
}

private ReactiveCounterPatch ReactiveCounter(int value)
{
    return new()
    {
        value = value,
        doubled = value * 2,
        positive = value > 0,
        canDecrease = value > 0,
        summary = $"Value {value}; doubled {value * 2}"
    };
}

public ReactiveCounterPatch IncreaseReactive(int value)
{
    return ReactiveCounter(value + 1);
}

public ReactiveCounterPatch DecreaseReactive(int value)
{
    return ReactiveCounter(value - 1);
}

private QueryProjection QueryProjectionFor(string query)
{
    return new()
    {
        length = query.Length,
        valid = query.Length >= 2,
        preview = query.Length == 0 ? "Nothing to search" : $"Search: {query}"
    };
}

public QueryProjection ProjectQuery(string query)
{
    return QueryProjectionFor(query);
}

public async Task<QueryProjection> ProjectQueryLater(string query)
{
    await Task.Delay(query.Length == 1 ? 30 : 1);
    return QueryProjectionFor(query);
}

private CounterProjectionState CounterProjection(int count)
{
    return new()
    {
        count = count,
        disabled = count <= 0,
        summary = $"Projected count: {count}",
        className = count == 0 ? "at-zero" : "positive"
    };
}

public CounterProjectionState IncreaseProjected(int count)
{
    return CounterProjection(count + 1);
}

public CounterProjectionState DecreaseProjected(int count)
{
    return CounterProjection(count - 1);
}

private RowSelectionProjectionState InitialRowSelection()
{
    return new()
    {
        selectedId = 1,
        selectedLabel = "Selected row 1",
        firstClass = "selected",
        secondClass = "",
        thirdClass = ""
    };
}

public RowSelectionProjectionTransition SelectSecondAndRemoveFirst(int selectedId)
{
    int nextId = selectedId == 1 ? 2 : selectedId;
    return new()
    {
        state = new()
        {
            selectedId = nextId,
            selectedLabel = $"Selected row {nextId}",
            firstClass = "",
            secondClass = "selected",
            thirdClass = ""
        },
        removal = RemoveKey("projection-row-1")
    };
}

public bool ValidateName(string name)
{
    return name.Length >= 2;
}

public string SubmitName(string name)
{
    if (!ValidateName(name)) { return ""; }
    return $"Thanks, {name}.";
}

public KeyedRemove RemoveTodo(string key)
{
    return RemoveKey(key);
}

private List<NativeTodo> NativeTodos()
{
    List<NativeTodo> todos = new();
    todos.Add(new() { key = "native-1", title = "Buy milk" });
    todos.Add(new() { key = "native-2", title = "Walk dog" });
    todos.Add(new() { key = "native-3", title = "Write Aster" });
    return todos;
}

private Html NativeTodoRows(List<NativeTodo> todos)
{
    List<Html> rows = new();
    foreach (NativeTodo todo in todos)
    {
        rows.Add(
            <li key=todo.key>
                <label>
                    {todo.title}
                    <input value=todo.title />
                </label>
                <button
                    type="button"
                    name="key"
                    value=todo.key
                    aria-controls="native-keyed-list"
                    onclick=RemoveNativeTodo
                >Remove</button>
            </li>
        );
    }
    return <>{rows}</>;
}

public Html RemoveNativeTodo(string key)
{
    List<NativeTodo> todos = NativeTodos();
    for (nuint index = 0; index < todos.Count; index++)
    {
        if (todos[index].key == key)
        {
            todos.RemoveAt(index);
            break;
        }
    }
    return NativeTodoRows(todos);
}

public Html AppendNativeTodo()
{
    List<NativeTodo> todos = NativeTodos();
    todos.Add(new() { key = "native-4", title = "Ship keyed lists" });
    return NativeTodoRows(todos);
}

public Html MoveNativeTodo()
{
    List<NativeTodo> todos = NativeTodos();
    NativeTodo last = todos[2];
    todos.RemoveAt(2);
    todos.Insert(0, last);
    return NativeTodoRows(todos);
}

public Html ClearNativeTodos()
{
    List<NativeTodo> todos = NativeTodos();
    todos.Clear();
    return NativeTodoRows(todos);
}

private Html TodoItem(int id, string title)
{
    string key = $"todo-{id}";
    return <li id=key>
        <span>{title}</span>
        <button
            type="button"
            name="key"
            value=key
            aria-controls="todo-list"
            onclick=RemoveTodo
        >Remove</button>
    </li>;
}

public TodoPatch AddTodo(int nextId, string title)
{
    return new()
    {
        nextId = nextId + 1,
        item = TodoItem(nextId, title)
    };
}

public Html ReplaceMessage(string message)
{
    return <p id="latest-message">{message}</p>;
}

public Html BrowserPage(Html browserLoader)
{
    QueryProjection initialQuery = ProjectQuery("");
    CounterProjectionState initialCounter = CounterProjection(1);
    RowSelectionProjectionState initialRows = InitialRowSelection();
    return <main>
        <h1>Lime Browser 0.1</h1>
        <section id="counter-island">
            <output name="count">1</output>
            <button type="button" onclick=Increment>Increment</button>
            <button type="button" onclick=IncrementLater>
                Increment later
            </button>
            <button type="button" hidden=true onclick=FailLater>
                Fail later
            </button>
        </section>
        <section id="reactive-counter">
            <h2>Reactive projection trial</h2>
            <output name="value">1</output>
            <output name="value">1</output>
            <output name="doubled">2</output>
            <output name="summary">Value 1; doubled 2</output>
            <output name="positive">The value is positive.</output>
            <button type="button" onclick=IncreaseReactive>
                Increase
            </button>
            <button
                type="button"
                name="canDecrease"
                onclick=DecreaseReactive
            >
                Decrease
            </button>
        </section>
        <section id="compiled-projection-trial">
            <h2>Compiled projection batch trial</h2>
            <output project_text=initialCounter.count>
                {initialCounter.count}
            </output>
            <output project_text=initialCounter.summary>
                {initialCounter.summary}
            </output>
            <p project_class=initialCounter.className>
                State class projection
            </p>
            <button type="button" onclick=IncreaseProjected>
                Increase projected
            </button>
            <button
                type="button"
                project_disabled=initialCounter.disabled
                onclick=DecreaseProjected
            >Decrease projected</button>
        </section>
        <section id="compiled-row-transition">
            <h2>Composed keyed projection trial</h2>
            <output project_text=initialRows.selectedId>
                {initialRows.selectedId}
            </output>
            <output project_text=initialRows.selectedLabel>
                {initialRows.selectedLabel}
            </output>
            <ul id="projection-row-list">
                <li
                    id="projection-row-1"
                    project_class=initialRows.firstClass
                >First row</li>
                <li
                    id="projection-row-2"
                    project_class=initialRows.secondClass
                >
                    Second row
                    <input
                        id="projection-row-input"
                        value="browser-owned value"
                    />
                </li>
                <li
                    id="projection-row-3"
                    project_class=initialRows.thirdClass
                >Third row</li>
            </ul>
            <button
                type="button"
                aria-controls="projection-row-list"
                onclick=SelectSecondAndRemoveFirst
            >Select second and remove first</button>
        </section>
        <PersistentTodoList />
        <IsolatedCounter />
        <IsolatedCounter />
        <SeededCounter label="Alpha" initial=7 enabled=true />
        <SeededCounter label="Beta" initial=40 enabled=false />
        <AsyncTodoComponent status="idle-one" />
        <AsyncTodoComponent status="idle-two" />
        <section id="component-drop-probe">
            <output name="dropCount">0</output>
            <button type="button" onclick=ReadDroppedCounters>
                Read dropped counters
            </button>
            <output name="nestedDropCount">0</output>
            <button type="button" onclick=ReadNestedDrops>
                Read nested drops
            </button>
            <output name="constructionAttempts">0</output>
            <button type="button" onclick=ReadConstructionAttempts>
                Read construction attempts
            </button>
            <output name="asyncDropCount">0</output>
            <button type="button" onclick=ReadAsyncComponentDrops>
                Read async component drops
            </button>
        </section>
        <section id="native-keyed-list-trial">
            <h2>Native keyed list trial</h2>
            <ul id="native-keyed-list">{NativeTodoRows(NativeTodos())}</ul>
            <button
                type="button"
                aria-controls="native-keyed-list"
                onclick=AppendNativeTodo
            >Append native todo</button>
            <button
                type="button"
                aria-controls="native-keyed-list"
                onclick=MoveNativeTodo
            >Move native todo</button>
            <button
                type="button"
                aria-controls="native-keyed-list"
                onclick=ClearNativeTodos
            >Clear native todos</button>
        </section>
        <form
            id="reactive-query"
            action=Url.relative("/search")
            method="get"
        >
            <label for="reactive-query-input">Query</label>
            <input
                id="reactive-query-input"
                name="query"
                oninput=ProjectQuery
            />
            <output name="length">{initialQuery.length}</output>
            <output name="preview">{initialQuery.preview}</output>
            <button
                type="submit"
                name="valid"
                disabled=!initialQuery.valid
            >Search</button>
        </form>
        <form
            id="async-query"
            action=Url.relative("/search")
            method="get"
        >
            <label for="async-query-input">Async query</label>
            <input
                id="async-query-input"
                name="query"
                oninput=ProjectQueryLater
            />
            <output name="length">{initialQuery.length}</output>
            <output name="preview">{initialQuery.preview}</output>
            <button
                type="submit"
                name="valid"
                disabled=!initialQuery.valid
            >Search</button>
        </form>
        <form
            id="contact"
            action=Url.relative("/contact")
            method="post"
            onsubmit=SubmitName
        >
            <label for="contact-name">Name</label>
            <input
                id="contact-name"
                name="name"
                required=true
                aria-describedby="contact-name-error"
                oninput=ValidateName
            />
            <p id="contact-name-error" hidden=true>
                Enter at least two characters.
            </p>
            <button type="submit">Send</button>
            <p id="contact-success" hidden=true>Accepted.</p>
            <p id="contact-error" hidden=true>Check the form.</p>
        </form>
        <form
            id="todos"
            action=Url.relative("/todo")
            method="post"
            aria-controls="todo-list"
            onsubmit=AddTodo
        >
            <input name="title" required=true />
            <output name="nextId" hidden=true>1</output>
            <button type="submit">Add</button>
            <ul id="todo-list"><TodoItem id=0 title="First" /></ul>
        </form>
        <form
            id="message"
            action=Url.relative("/message")
            method="post"
            aria-controls="message-output"
            onsubmit=ReplaceMessage
        >
            <input name="message" required=true />
            <button type="submit">Replace</button>
            <div id="message-output">
                <p id="latest-message">Initial message</p>
            </div>
        </form>
        {browserLoader}
    </main>;
}

int main()
{
    Console.WriteLine(BrowserPage(<></>).ToHtmlString());
    return 0;
}
