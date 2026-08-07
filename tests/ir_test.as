private extern NativeHandle NativeHandleOpenId(long id);

private struct Marker {
    long id;
}

~Marker() {
    Console.WriteLine(self.id);
}

private struct Pair {
    long first;
    long second;
}

private enum Phase {
    Start,
    Done,
}

private element Html section {
    Html children;
}

private element Html strong {
    Html children;
}

private element Html style {
    Html children;
}

private Html Badge(string label) {
    return <strong>{label}</strong>;
}

private Html render(bool show, string labels[2]) {
    return <section>
        <style scoped>.featured { color: red; }</style>
        if (show) {
            <Badge label="Featured" />
        }
        foreach (string label in labels) {
            <strong>{label}</strong>
        }
    </section>;
}

private void consume(NativeHandle handle) {
}

private long factorial(long n) {
    long result = 1;
    long value = n;
    while (value > 1) {
        result = result * value;
        value = value - 1;
    }
    return result;
}

private Result<long, string> fail() {
    return Result.Err("failed");
}

private Result<long, string> propagate() {
    NativeHandle cleanup = NativeHandleOpenId(9);
    long value = try fail();
    return Result.Ok(value);
}

private long OptionValue(Option<long> option) {
    switch (option) {
        case Option.Some(value): {
            return value;
        }
        case Option.None: {
            return 0;
        }
    }
}

private long PhaseValue(Phase phase) {
    switch (phase) {
        case Phase.Start: {
            return 1;
        }
        case Phase.Done: {
            return 2;
        }
    }
}

int main() {
    Marker marker = Marker { id: 99 };
    NativeHandle handle = NativeHandleOpenId(7);
    consume(handle);

    Pair pair = Pair {
        second: 2,
        first: 1,
    };
    pair.first = 9;
    Console.WriteLine(pair.second);

    long values[2] = [3, 4];
    values[0] = 5;
    Console.WriteLine(values[1]);
    long total = 0;
    foreach (long item in values) {
        total = total + item;
    }
    Console.WriteLine(total);

    Arena arena = Arena.new();
    unsafe {
        long* pointer = ArenaAlloc(arena, 8);
        *pointer = 11;
        Console.WriteLine(*pointer);
    }

    Option<long> present = Option.Some(8);
    Console.WriteLine(OptionValue(present));
    Result<long, string> propagated = propagate();
    Phase phase = Phase.Done;
    Console.WriteLine(PhaseValue(phase));
    Console.WriteLine(factorial(6));
    return 0;
}
