namespace Tests.BrowserApp;

using Aster.Html;
using System.Text;

public struct TodoPatch
{
    int nextId;
    Html item;
}

public int Increment(int count)
{
    return count + 1;
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
