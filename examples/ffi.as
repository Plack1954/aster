private extern long NativeAdd(long left, long right);
private extern long NativeClock();
private extern NativeHandle NativeHandleOpen();

int main() {
    Console.WriteLine(NativeAdd(20, 22));
    long timestamp = NativeClock();
    NativeHandle handle = NativeHandleOpen();
    return 0;
}
