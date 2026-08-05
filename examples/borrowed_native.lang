private extern NativeHandle NativeHandleOpenId(long id);
private extern long NativeHandleId(NativeHandle handle);
private extern long NativeHandleDropLog();

int main() {
    {
        NativeHandle handle = NativeHandleOpenId(7);
        Console.WriteLine(NativeHandleId(handle));
        Console.WriteLine(NativeHandleId(handle));
        Console.WriteLine(NativeHandleDropLog());
    }

    Console.WriteLine(NativeHandleDropLog());
    return 0;
}
