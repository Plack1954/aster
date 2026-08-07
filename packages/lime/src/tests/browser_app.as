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
                    <NestedCounter />
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
    private bool renderFault;
    private static int droppedCount;

    public IsolatedCounter()
    {
        this.count = 0;
        this.renderFault = false;
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

    private void FailRender()
    {
        this.renderFault = true;
    }

    private void RecoverRender()
    {
        this.renderFault = false;
    }

    public Html Render()
    {
        if (this.renderFault)
        {
            throw new Exception("isolated component render failure");
        }
        return <section class="isolated-counter">
            <output name="count">0</output>
            <button type="button" onclick=this.Increment>
                Increment isolated counter
            </button>
            <button type="button" onclick=this.Fail>
                Fail isolated counter
            </button>
            <button type="button" onclick=this.FailRender>
                Fail isolated render
            </button>
            <button type="button" onclick=this.RecoverRender>
                Recover isolated render
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

private class FaultingDestructorComponent
{
    private static int attempts;

    public FaultingDestructorComponent()
    {
    }

    ~FaultingDestructorComponent()
    {
        attempts += 1;
        throw new Exception("component destructor failure");
    }

    public static int Attempts => attempts;

    private int TouchDestructor(int destructorValue)
    {
        return destructorValue + 1;
    }

    public Html Render()
    {
        return <section class="faulting-destructor-component">
            <output name="destructorValue">0</output>
            <button type="button" onclick=this.TouchDestructor>Touch destructor component</button>
        </section>;
    }
}

public int ReadDestructorAttempts(int destructorAttempts)
{
    return FaultingDestructorComponent.Attempts;
}

public struct ReactiveCounterPatch
{
    int value;
    int doubled;
    bool positive;
    bool canDecrease;
    string summary;
}

public struct QueryPatch
{
    nuint length;
    bool valid;
    string preview;
}

private struct SelectionRow
{
    string key;
    string label;
    string className;
}

private class ProjectedCounter
{
    private int count;

    public ProjectedCounter()
    {
        this.count = 1;
    }

    private void Increase()
    {
        this.count += 1;
    }

    private void Decrease()
    {
        this.count -= 1;
    }

    public Html Render()
    {
        string summary = $"Projected count: {this.count}";
        string className = this.count == 0 ? "at-zero" : "positive";
        return <section id="inferred-counter-trial">
            <h2>Inferred counter parts</h2>
            <output>{this.count}</output>
            <output>{summary}</output>
            <p class=className>State class part</p>
            <button type="button" onclick=this.Increase>Increase projected</button>
            <button type="button" disabled=this.count <= 0 onclick=this.Decrease>Decrease projected</button>
        </section>;
    }
}

private class RowSelectionComponent
{
    private List<SelectionRow> rows;
    private int selectedId;

    public RowSelectionComponent()
    {
        rows = new();
        rows.Add(new() { key = "selection-row-1", label = "First row", className = "selected" });
        rows.Add(new() { key = "selection-row-2", label = "Second row", className = "" });
        rows.Add(new() { key = "selection-row-3", label = "Third row", className = "" });
        this.selectedId = 1;
    }

    private void SelectSecondAndRemoveFirst()
    {
        this.selectedId = 2;
        for (nuint index = 0; index < this.rows.Count; index++)
        {
            if (this.rows[index].key == "selection-row-1")
            {
                this.rows.RemoveAt(index);
                break;
            }
        }
        for (nuint index = 0; index < this.rows.Count; index++)
        {
            SelectionRow row = this.rows[index];
            row.className = row.key == "selection-row-2" ? "selected" : "";
            this.rows.Set(index, row);
        }
    }

    public Html Render()
    {
        List<Html> rendered = new();
        foreach (SelectionRow row in this.rows)
        {
            rendered.Add(<li key=row.key class=row.className>
                {row.label}
                <input value="browser-owned value" />
            </li>);
        }
        return <section id="compiled-row-transition">
            <h2>Composed keyed native parts</h2>
            <output>{this.selectedId}</output>
            <output>Selected row {this.selectedId}</output>
            <ul id="selection-row-list">{rendered}</ul>
            <button type="button" aria-controls="selection-row-list" onclick=this.SelectSecondAndRemoveFirst>Select second and remove first</button>
        </section>;
    }
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

private QueryPatch QueryPatchFor(string query)
{
    return new()
    {
        length = query.Length,
        valid = query.Length >= 2,
        preview = query.Length == 0 ? "Nothing to search" : $"Search: {query}"
    };
}

public QueryPatch ProjectQuery(string query)
{
    return QueryPatchFor(query);
}

public async Task<QueryPatch> ProjectQueryLater(string query)
{
    await Task.Delay(query.Length == 1 ? 30 : 1);
    return QueryPatchFor(query);
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
    return <li id=key><span>{title}</span></li>;
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
    QueryPatch initialQuery = ProjectQuery("");
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
            <h2>Aggregate state trial</h2>
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
        <ProjectedCounter />
        <RowSelectionComponent />
        <PersistentTodoList />
        <IsolatedCounter />
        <IsolatedCounter />
        <SeededCounter label="Alpha" initial=7 enabled=true />
        <SeededCounter label="Beta" initial=40 enabled=false />
        <AsyncTodoComponent status="idle-one" />
        <AsyncTodoComponent status="idle-two" />
        <FaultingDestructorComponent />
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
            <output name="destructorAttempts">0</output>
            <button type="button" onclick=ReadDestructorAttempts>
                Read destructor attempts
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
