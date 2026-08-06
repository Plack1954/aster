struct CopyItem {
    long value;

    public CopyItem(const ref CopyItem other) {
        value = other.value + 1;
    }
}

union CopyUnion {
    Empty,
    Value(CopyItem),
}

int main() {
    Option<CopyItem> optionOriginal = Option.Some(
        new() { value = 10 });
    Option<CopyItem> optionCopy = optionOriginal;
    Option<CopyItem> noneCopy = Option.None;
    switch (optionOriginal) {
        case Option.Some(CopyItem item): {
            Console.WriteLine(item.value);
        }
        case Option.None: {}
    }
    switch (optionCopy) {
        case Option.Some(CopyItem item): {
            Console.WriteLine(item.value);
        }
        case Option.None: {}
    }
    switch (noneCopy) {
        case Option.Some(CopyItem item): {
            Console.WriteLine(item.value);
        }
        case Option.None: {
            Console.WriteLine(0);
        }
    }

    Result<CopyItem, CopyItem> okOriginal = Result.Ok(
        new() { value = 20 });
    Result<CopyItem, CopyItem> okCopy = okOriginal;
    switch (okCopy) {
        case Result.Ok(CopyItem item): {
            Console.WriteLine(item.value);
        }
        case Result.Err(CopyItem error): {
            Console.WriteLine(error.value);
        }
    }

    Result<CopyItem, CopyItem> errOriginal = Result.Err(
        new() { value = 30 });
    Result<CopyItem, CopyItem> errCopy = errOriginal;
    switch (errCopy) {
        case Result.Ok(CopyItem item): {
            Console.WriteLine(item.value);
        }
        case Result.Err(CopyItem error): {
            Console.WriteLine(error.value);
        }
    }

    CopyItem unionItem = new() { value = 40 };
    CopyUnion unionOriginal = CopyUnion.Value(unionItem);
    CopyUnion unionCopy = unionOriginal;
    switch (unionCopy) {
        case CopyUnion.Value(CopyItem item): {
            Console.WriteLine(item.value);
        }
        case CopyUnion.Empty: {
            Console.WriteLine(-1);
        }
    }
    return 0;
}
