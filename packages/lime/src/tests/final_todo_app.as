namespace Tests.FinalTodoApp;

using Aster.Html;

private extern Task Task.Delay(int milliseconds);

private struct FinalTodo
{
    string key;
    string title;
    string className;
    bool saved;
}

private class TodoBadge
{
    private string label;
    private int touches;
    private static int dropped;

    public TodoBadge(string label)
    {
        this.label = label;
        this.touches = 0;
    }

    ~TodoBadge()
    {
        dropped += 1;
    }

    public static int Dropped => dropped;

    private int Touch(int touches)
    {
        this.touches += 1;
        return this.touches;
    }

    public Html Render()
    {
        return <aside class="todo-badge">
            <span>{this.label}</span>
            <output name="touches">0</output>
            <button type="button" onclick=this.Touch>Touch badge</button>
        </aside>;
    }
}

private class FinalTodoList
{
    private List<FinalTodo> todos;
    private string region;
    private string firstTitle;
    private int start;
    private int nextId;
    private static int dropped;

    public FinalTodoList(string region, string firstTitle, int start)
    {
        todos = new();
        this.region = region;
        this.firstTitle = firstTitle;
        this.start = start;
        this.nextId = start + 2;
        todos.Add(new()
        {
            key = $"{region}-{start}",
            title = firstTitle,
            className = "server-loaded",
            saved = true
        });
        todos.Add(new()
        {
            key = $"{region}-{start + 1}",
            title = $"{firstTitle} follow-up",
            className = "",
            saved = false
        });
    }

    ~FinalTodoList()
    {
        dropped += 1;
    }

    public static int Dropped => dropped;

    private void Append()
    {
        int id = this.nextId;
        this.nextId += 1;
        FinalTodo todo = new()
        {
            key = $"{this.region}-{id}",
            title = $"Todo {id}",
            className = "",
            saved = false
        };
        this.todos.Add(todo);
    }

    private void Rename(string key)
    {
        for (nuint index = 0; index < this.todos.Count; index++)
        {
            if (this.todos[index].key == key)
            {
                FinalTodo todo = this.todos[index];
                todo.title = $"{todo.title}!";
                todo.className = "renamed";
                this.todos.Set(index, todo);
                return;
            }
        }
    }

    private void Remove(string key)
    {
        for (nuint index = 0; index < this.todos.Count; index++)
        {
            if (this.todos[index].key == key)
            {
                this.todos.RemoveAt(index);
                return;
            }
        }
    }

    private void Reorder()
    {
        if (this.todos.Count < 2) { return; }
        FinalTodo first = this.todos[0];
        FinalTodo second = this.todos[1];
        this.todos.Set(0, second);
        this.todos.Set(1, first);
    }

    private void Clear()
    {
        this.todos.Clear();
    }

    private async Task Save(string key)
    {
        await Task.Delay(10);
        for (nuint index = 0; index < this.todos.Count; index++)
        {
            if (this.todos[index].key == key)
            {
                FinalTodo todo = this.todos[index];
                todo.saved = true;
                todo.className = "saved";
                this.todos.Set(index, todo);
                return;
            }
        }
    }

    private async Task FailSave(string key)
    {
        await Task.Delay(1);
        throw new Exception($"save failed for {key}");
    }

    private Html Rows()
    {
        string listId = $"{this.region}-todo-list";
        List<Html> rows = new();
        foreach (FinalTodo todo in this.todos)
        {
            rows.Add(<li key=todo.key class=todo.className>
                <label>
                    <span>{todo.title}</span>
                    <input value=todo.title />
                    <small hidden=todo.saved>Pending save</small>
                    <small hidden=!todo.saved>Saved</small>
                </label>
                <TodoBadge label=todo.title />
                <button type="button" name="key" value=todo.key aria-controls=listId onclick=this.Rename>Rename</button>
                <button type="button" name="key" value=todo.key aria-controls=listId onclick=this.Save>Save</button>
                <button type="button" name="key" value=todo.key aria-controls=listId onclick=this.FailSave>Fail save</button>
                <button type="button" name="key" value=todo.key aria-controls=listId onclick=this.Remove>Remove</button>
            </li>);
        }
        return <>{rows}</>;
    }

    public Html Render()
    {
        string listId = $"{this.region}-todo-list";
        return <section class="final-todo-list" data-region=this.region>
            <h2>{this.firstTitle}</h2>
            <ul id=listId>{this.Rows()}</ul>
            <button type="button" aria-controls=listId onclick=this.Append>Append</button>
            <button type="button" aria-controls=listId onclick=this.Reorder>Reorder</button>
            <button type="button" aria-controls=listId onclick=this.Clear>Clear</button>
        </section>;
    }
}

public int ReadFinalTodoDrops(int finalTodoDrops)
{
    return FinalTodoList.Dropped;
}

public int ReadTodoBadgeDrops(int todoBadgeDrops)
{
    return TodoBadge.Dropped;
}

public Html FinalTodoPage(Html loader)
{
    return <main id="final-todo-proof">
        <h1>Server-loaded todos</h1>
        <FinalTodoList region="alpha" firstTitle="Server alpha" start=10 />
        <FinalTodoList region="beta" firstTitle="Server beta" start=20 />
        <output name="finalTodoDrops">0</output>
        <button type="button" onclick=ReadFinalTodoDrops>Read todo drops</button>
        <output name="todoBadgeDrops">0</output>
        <button type="button" onclick=ReadTodoBadgeDrops>Read badge drops</button>
        {loader}
    </main>;
}

int main()
{
    Console.WriteLine(FinalTodoPage(<></>).ToHtmlString());
    return 0;
}
