namespace Shared;

public struct Box<T> {
    T value;
}

public struct Plain {
    long value;
}

public T identity<T>(T value) {
    return value;
}

namespace First;

using Shared;

private void AcceptFirst(Shared.Box<long> value) {
    Console.WriteLine(value.value);
}

private void AcceptPlainFirst(Shared.Plain value) {
    Console.WriteLine(value.value);
}

private long CallFirst(long value) {
    return Shared.identity(value);
}

namespace Second;

using Shared;

private void AcceptSecond(Shared.Box<long> value) {
    Console.WriteLine(value.value);
}

private void AcceptPlainSecond(Shared.Plain value) {
    Console.WriteLine(value.value);
}

private long CallSecond(long value) {
    return Shared.identity(value);
}

int main() {
    return 0;
}
