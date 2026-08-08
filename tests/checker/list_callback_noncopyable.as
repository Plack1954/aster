private struct Owner {
    Arena storage;
}

private void Consume(Owner value) {
}

int main() {
    List<Owner> values = new();
    values.ForEach(Consume);
    return 0;
}
