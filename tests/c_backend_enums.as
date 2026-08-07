private union State {
    Empty,
    Value(int),
}

private union Maybe<T> {
    None,
    Some(T),
}

private enum Phase {
    Start,
    Done,
}

private int PhaseValue(Phase phase) {
    switch (phase) {
        case Phase.Start: {
            return 1;
        }
        case Phase.Done: {
            return 3;
        }
    }
}

private int StateValue(State state) {
    switch (state) {
        case State.Value(int value): {
            return value;
        }
        case State.Empty: {
            return 0;
        }
    }
}

private int MaybeValue(Maybe<int> value) {
    switch (value) {
        case Maybe.Some(int inner): {
            return inner;
        }
        case Maybe.None: {
            return 0;
        }
    }
}

private Result<int, int> source(bool succeed) {
    if (succeed) {
        return Result.Ok(41);
    }
    return Result.Err(-7);
}

private Result<int, int> through(bool succeed) {
    int value = try source(succeed);
    return Result.Ok(value + 1);
}

private int ResultValue(Result<int, int> value) {
    switch (value) {
        case Result.Ok(int inner): {
            return inner;
        }
        case Result.Err(int error): {
            return error;
        }
    }
}

int main() {
    int state = StateValue(State.Value(6));
    int empty = StateValue(State.Empty);
    int maybe = MaybeValue(Maybe.Some(9));
    int none = MaybeValue(Maybe.None);
    int success = ResultValue(through(true));
    int failure = ResultValue(through(false));
    int phase = PhaseValue(Phase.Done);

    int total =
        state + empty + maybe + none + success + failure + phase;
    if (total == 53) {
        return 0;
    }
    return 1;
}
