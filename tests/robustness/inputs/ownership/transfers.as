namespace Robustness.Ownership;

struct Resource {
    string name;
}

private long Consume(Resource value) {
    return value.name.Length;
}

private long BorrowThenConsume(
    const ref Resource borrowed,
    Resource owned
) {
    return borrowed.name.Length + owned.name.Length;
}

int main() {
    Resource value = new() { name = "seed" };
    return BorrowThenConsume(value, value) + Consume(value);
}
