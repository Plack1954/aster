namespace Aster.Core;

// Option and Result storage/lowering remain compiler primitives. These are
// ordinary convenience operations over those built-in tagged values.

public bool OptionIsSome<T>(Option<T> value) {
    switch (value) {
        case Option.Some(payload): { return true; }
        case Option.None: { return false; }
    }
    return false;
}

public bool OptionIsNone<T>(Option<T> value) {
    return !OptionIsSome(value);
}

public bool ResultIsOk<T, E>(Result<T, E> value) {
    switch (value) {
        case Result.Ok(success): { return true; }
        case Result.Err(error): { return false; }
    }
    return false;
}

public bool ResultIsErr<T, E>(Result<T, E> value) {
    return !ResultIsOk(value);
}

public struct Pair<A, B> {
    A first;
    B second;
}

public Pair<A, B> pair<A, B>(A first, B second) {
    return new Pair {
        first = first,
        second = second,
    };
}
