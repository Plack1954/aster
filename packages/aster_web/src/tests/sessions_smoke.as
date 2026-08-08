namespace Tests.SessionsSmoke;

using Aster.Web;
using Aster.Web.Sessions;

private bool HeaderValueContains(
    const ref ResponseHeader header,
    const ref string value
)
{
    return header.Value.Contains(value);
}
using System.Text;

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

    string cookie = $"aster.session={first.Id()}";
    Session second = sessions.Open(MakeRequest(cookie));
    if (second.IsNew() || second.Id() != first.Id()) { return 3; }
    switch (second.GetString("user"))
    {
        case Option.Some(value): {
            if (value != "brandon") { return 4; }
        }
        case Option.None: { return 5; }
    }
    second.SetString("temporary", "value");
    second.Remove("temporary");
    switch (second.GetString("temporary"))
    {
        case Option.Some(value): { return 6; }
        case Option.None: {}
    }
    string previousId = second.Id();
    second.Rotate();
    if (second.Id() == previousId || second.Id().Length != 64)
    {
        return 7;
    }
    switch (second.GetString("user"))
    {
        case Option.Some(value): {
            if (value != "brandon") { return 8; }
        }
        case Option.None: { return 9; }
    }
    Response rotatedResponse = Results.Text("rotated");
    second.Commit(ref rotatedResponse);
    (int rotatedStatus, ResponseBody rotatedBody,
     List<ResponseHeader> rotatedHeaders) = rotatedResponse;
    if (rotatedHeaders.Count != 1 ||
        !HeaderValueContains(rotatedHeaders[0], second.Id()))
    {
        return 10;
    }

    Session fixationAttempt = sessions.Open(
        MakeRequest($"aster.session={previousId}")
    );
    if (!fixationAttempt.IsNew() || fixationAttempt.Id() == previousId)
    {
        return 11;
    }

    string rotatedId = second.Id();
    Session resumed = sessions.Open(
        MakeRequest($"aster.session={rotatedId}")
    );
    if (resumed.IsNew() || resumed.Id() != rotatedId) { return 12; }
    resumed.Destroy();
    Response destroyedResponse = Results.Text("destroyed");
    resumed.Commit(ref destroyedResponse);
    (int destroyedStatus, ResponseBody destroyedBody,
     List<ResponseHeader> destroyedHeaders) = destroyedResponse;
    if (destroyedHeaders.Count != 1 ||
        !HeaderValueContains(destroyedHeaders[0], "Max-Age=0"))
    {
        return 13;
    }
    bool rejectedDestroyed = false;
    try
    {
        Option<string> ignored = resumed.GetString("user");
    }
    catch (InvalidOperationException error)
    {
        rejectedDestroyed = error.Message.Contains("destroyed");
    }
    if (!rejectedDestroyed) { return 14; }

    Session revoked = sessions.Open(
        MakeRequest($"aster.session={rotatedId}")
    );
    if (!revoked.IsNew() || revoked.Id() == rotatedId) { return 15; }

    long swept = sessions.SweepExpired();
    if (swept != 0) { return 16; }

    SessionOptions frequentOptions = SessionOptions();
    frequentOptions.cleanupInterval = 1;
    SessionStore frequent = SessionStore.Create(":memory:", frequentOptions);
    Session frequentSession = frequent.Open(MakeRequest(""));
    if (!frequentSession.IsNew() || frequent.SweepExpired() != 0)
    {
        return 17;
    }
    return 0;
}
