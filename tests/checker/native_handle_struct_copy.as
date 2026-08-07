private extern NativeHandle NativeHandleOpenId(long id);

private struct Owner {
    NativeHandle handle;
}

int main() {
    NativeHandle handle = NativeHandleOpenId(1);
    Owner owner = Owner { handle: handle };
    Owner copied = owner;
    Console.WriteLine(owner.handle);
    return 0;
}
