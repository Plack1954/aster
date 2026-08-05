namespace Counter;

using Aster.Html;
using System.Text;

struct CounterState {
    int Count;
}

struct TodoPatch {
    int nextId;
    Html item;
}

public int Increment(int count) {
    return count + 1;
}

public int ResetCount(int count) {
    return 0;
}

public bool ToggleDetails(bool expanded) {
    return !expanded;
}

public string RemoveTodo(string removeKey) {
    return removeKey;
}

public bool ValidateName(string name) {
    return name.Length >= 2;
}

public bool ValidateEmail(string email) {
    return email.Length >= 5 && email.Contains("@");
}

public string SubmitContact(string name, string email) {
    if (!ValidateName(name) || !ValidateEmail(email)) {
        return "";
    }
    StringBuilder message = new();
    message.Append("Thanks, ");
    message.Append(name);
    message.Append(". We will reply to ");
    message.Append(email);
    message.Append(".");
    return message.ToString();
}

private Html TodoItem(int itemId, string title) {
    return <li id=$"todo-{itemId}">
        <span>{title}</span>
        <button
            type="button"
            name="removeKey"
            value=$"todo-{itemId}"
            aria-controls="todo-list"
            onclick=RemoveTodo
        >Remove</button>
    </li>;
}

public TodoPatch AddTodo(int nextId, string title) {
    return new() {
        nextId = nextId + 1,
        item = TodoItem(nextId, title)
    };
}

public Html ReplaceNotice(string notice) {
    return <p id="notice-current">{notice}</p>;
}

private Html NoticeForm() {
    return <form
        id="notice-island"
        action=Url.relative("/notice")
        method="post"
        aria-controls="notice-output"
        onsubmit=ReplaceNotice
    >
        <input name="notice" required=true />
        <button type="submit">Replace notice</button>
        <div id="notice-output">
            <p id="notice-current">Initial notice</p>
        </div>
    </form>;
}

private Html TodoList() {
    return <form
        id="todo-island"
        action=Url.relative("/todos")
        method="post"
        aria-controls="todo-list"
        onsubmit=AddTodo
    >
        <label for="todo-title">New item</label>
        <input id="todo-title" name="title" type="text" required=true />
        <output name="nextId" hidden=true>2</output>
        <button type="submit">Add item</button>
        <ul id="todo-list">
            <TodoItem itemId=0 title="Initial item" />
            <TodoItem itemId=1 title="Second item" />
        </ul>
    </form>;
}

private Html Counter(CounterState state) {
    return <section id="counter-island">
        <p>
            Clicked <output name="count">{state.Count}</output> times
        </p>
        <button
            type="button"
            onclick=Increment
            aria-label="Increment counter"
        >Increment</button>
        <button type="button" onclick=ResetCount>Reset</button>
    </section>;
}

private Html Disclosure(string islandId, string panelId) {
    return <section id=islandId>
        <button
            type="button"
            onclick=ToggleDetails
            aria-expanded="false"
            aria-controls=panelId
        >Project details</button>
        <div id=panelId hidden=true>
            Persistent Aster state controls this native HTML.
        </div>
    </section>;
}

private Html ContactForm(string formId) {
    return <form
        id=formId
        action=Url.relative("/contact")
        method="post"
        onsubmit=SubmitContact
    >
        <p>
            <label for=$"{formId}-name">Name</label>
            <input
                id=$"{formId}-name"
                type="text"
                name="name"
                required=true
                aria-describedby=$"{formId}-name-error"
                oninput=ValidateName
            />
        </p>
        <p id=$"{formId}-name-error" hidden=true>
            Enter at least two characters.
        </p>
        <p>
            <label for=$"{formId}-email">Email</label>
            <input
                id=$"{formId}-email"
                type="email"
                name="email"
                required=true
                aria-describedby=$"{formId}-email-error"
                oninput=ValidateEmail
            />
        </p>
        <p id=$"{formId}-email-error" hidden=true>
            Enter at least five characters.
        </p>
        <button type="submit">Send enquiry</button>
        <p id=$"{formId}-success" hidden=true>Enquiry accepted.</p>
        <p id=$"{formId}-error" hidden=true>
            Please correct the highlighted fields.
        </p>
    </form>;
}

int main() {
    CounterState state = new() {
        Count = Increment(3)
    };
    Html counter = Counter(state);
    Html page = <main>
        <h1>Aster browser islands</h1>
        {counter}
        <Disclosure
            islandId="details-primary"
            panelId="details-primary-panel"
        />
        <Disclosure
            islandId="details-secondary"
            panelId="details-secondary-panel"
        />
        <TodoList />
        <NoticeForm />
        <ContactForm formId="contact-primary" />
        <ContactForm formId="contact-secondary" />
    </main>;
    Console.WriteLine(page.ToHtmlString());
    return 0;
}
