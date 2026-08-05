namespace Tests.SessionsSmoke;

using Lime;
using Lime.Sessions;

private Request MakeRequest(string cookie)
{
    return RequestNew(
        "GET", "/account", "example.test", "", cookie, ""
    );
}

int main()
{
    SessionStore sessions = SessionStore.Create();
    Session first = sessions.Open(MakeRequest(""));
    if (!first.IsNew() || first.Id().Length != 64) { return 1; }
    first.SetString("user", "brandon");

    Response response = Results.Text("saved");
    first.Commit(ref response);
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    if (status != 200 || headers.Count != 1) { return 2; }

    string cookie = $"lime.session={first.Id()}";
    Session second = sessions.Open(MakeRequest(cookie));
    if (second.IsNew() || second.Id() != first.Id()) { return 3; }
    switch (second.GetString("user"))
    {
        case Option.Some(value): {
            if (value != "brandon") { return 4; }
        }
        case Option.None: { return 5; }
    }
    second.Remove("user");
    switch (second.GetString("user"))
    {
        case Option.Some(value): { return 6; }
        case Option.None: {}
    }
    second.SetString("flash", "saved");
    second.Clear();
    switch (second.GetString("flash"))
    {
        case Option.Some(value): { return 7; }
        case Option.None: {}
    }
    return 0;
}
