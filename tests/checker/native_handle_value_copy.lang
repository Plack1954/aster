private extern NativeHandle NativeHandleOpenId(long id);
private extern long NativeHandleId(NativeHandle handle);

int main() {
    NativeHandle handle = NativeHandleOpenId(1);
    NativeHandle moved = handle;
    Console.WriteLine(NativeHandleId(handle));
    return 0;
}
