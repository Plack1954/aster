namespace Shared.Generics;

public struct Box<T> {
    T value;
}

public union Maybe<T> {
    None,
    Some(T),
}

public T identity<T>(T value) {
    return value;
}
