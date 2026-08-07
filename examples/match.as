private union Status {
    Ready(long),
    Empty,
}

private long describe(Status status) {
    switch (status) {
        case Status.Ready(value): {
            return value;
        }
        case Status.Empty: {
            return 0;
        }
    }
}

int main() {
    Console.WriteLine(describe(Status.Ready(42)));
    Console.WriteLine(describe(Status.Empty));
    return 0;
}
