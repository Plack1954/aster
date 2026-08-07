private struct Pair {
    Result<long, long> result;
}

int main() {
    Result<long, long> first = Result.Ok(42);
    Result<long, long> second = first;
    var pair = new Pair { result = second };
    var copied = pair;

    switch (copied.result) {
        case Result.Ok(value): { Console.WriteLine(value); }
        case Result.Err(error): { Console.WriteLine(error); }
    }
    return 0;
}
