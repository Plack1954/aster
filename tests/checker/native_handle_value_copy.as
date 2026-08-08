private extern NativeHandle NativeHandleOpenId(long id);
private extern long NativeHandleId(const ref NativeHandle handle);

int main() {
    NativeHandle handle = NativeHandleOpenId(1);
    NativeHandle moved = handle;
    Console.WriteLine(NativeHandleId(moved));
    return 0;
}
