using System.Text;

private struct State
{
    string name;
    int count;
}

private delegate int Read(State state, int amount);
private delegate void Update(ref State state, int amount);

private int read(State state, int amount)
{
    return state.count + amount;
}

private void update(ref State state, int amount)
{
    state.count += amount;
}

int main()
{
    State state = new()
    {
        name = "lime",
        count = 1
    };
    Read reader = read;
    Update updater = update;
    Console.WriteLine(reader(state, 2));
    updater(state, 3);
    Console.WriteLine(reader(state, 2));
    Console.WriteLine(state.name);
    return 0;
}
