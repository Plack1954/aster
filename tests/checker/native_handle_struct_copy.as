private extern NativeHandle NativeHandleOpenId(long id);
private extern long NativeHandleId(const ref NativeHandle handle);

private struct Owner {
    NativeHandle handle;
}

int main() {
    NativeHandle handle = NativeHandleOpenId(1);
    Owner owner = Owner { handle: handle };
    Owner copied = owner;
    Console.WriteLine(NativeHandleId(copied.handle));
    return 0;
}
