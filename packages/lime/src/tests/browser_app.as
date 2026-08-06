namespace Tests.BrowserApp;

using Aster.Html;
using System.Text;

public struct TodoPatch
{
    int nextId;
    Html item;
}

public struct ReactiveCounterPatch
{
    int value;
    int doubled;
    bool positive;
    string summary;
}

private extern Task Task.Delay(int milliseconds);

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

public bool ValidateName(string name)
{
    return name.Length >= 2;
}

public string SubmitName(string name)
{
    if (!ValidateName(name)) { return ""; }
    return $"Thanks, {name}.";
}

private Html TodoItem(int id, string title)
{
    return <li id=$"todo-{id}">
        <span>{title}</span>
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
            <button type="button" onclick=DecreaseReactive>
                Decrease
            </button>
        </section>
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
