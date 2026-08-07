private extern NativeHandle NativeHandleOpenId(long id);
private extern long NativeHandleDropLog();

private struct Handles {
    NativeHandle first;
    NativeHandle second;
}

private union Resource {
    Handle(NativeHandle),
    Empty,
}

private void ConsumeHandles(Handles value) {
}

private void ConsumeArray(NativeHandle value[2]) {
}

int main() {
    NativeHandle first = NativeHandleOpenId(1);
    NativeHandle second = NativeHandleOpenId(2);
    Handles handles = new() {
        first = first,
        second = second,
    };
    ConsumeHandles(handles);
    Console.WriteLine(NativeHandleDropLog());

    NativeHandle third = NativeHandleOpenId(3);
    NativeHandle fourth = NativeHandleOpenId(4);
    NativeHandle array[2] = [third, fourth];
    ConsumeArray(array);
    Console.WriteLine(NativeHandleDropLog());

    {
        NativeHandle fifth = NativeHandleOpenId(5);
        Resource resource = Resource.Handle(fifth);
    }
    Console.WriteLine(NativeHandleDropLog());
    return 0;
}
