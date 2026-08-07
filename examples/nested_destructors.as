private struct Tracked {
    long id;
}

~Tracked() {
    Console.WriteLine(self.id);
}

private struct Wrapper {
    Tracked item;
}

private union MaybeTracked {
    Some(Tracked),
    None,
}

int main() {
    {
        Tracked item = new() { id = 7 };
        Wrapper wrapper = new() { item = item };
    }
    {
        Tracked item = new() { id = 8 };
        Tracked values[1] = [item];
    }
    {
        Tracked item = new() { id = 9 };
        MaybeTracked tagged = MaybeTracked.Some(item);
    }
    new Tracked { id = 10 };
    return 0;
}
