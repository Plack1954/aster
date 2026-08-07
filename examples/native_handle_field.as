private extern NativeHandle NativeHandleOpenId(long id);
private extern long NativeHandleDropLog();

private struct Wrapper {
    NativeHandle handle;
    long label;
}

int main() {
    {
        var wrapper = new Wrapper {
            handle = NativeHandleOpenId(7),
            label = 42,
        };
        Console.WriteLine(wrapper.label);
        Console.WriteLine(NativeHandleDropLog());
    }
    Console.WriteLine(NativeHandleDropLog());
    return 0;
}
