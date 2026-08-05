using Aster.Html;

union ViewState {
    Ready(string),
    Empty,
}

private Html StateView(ViewState state) {
    return <section>
        switch (state) {
            case ViewState.Ready(message): {
                <strong>{message}</strong>
            }
            case ViewState.Empty: {
                <p>Nothing yet</p>
            }
        }
    </section>;
}

int main() {
    Html output = StateView(ViewState.Ready("Loaded"));
    Console.WriteLine(output.ToHtmlString());
    return 0;
}
