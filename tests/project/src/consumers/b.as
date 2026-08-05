namespace Consumers.B;

using Shared.Generics;

public long read(Box<long> value) {
    return value.value;
}

public long echo(long value) {
    return identity(value);
}
