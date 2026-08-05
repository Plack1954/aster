private extern NativeHandle NativeHandleOpenId(long id);
private extern void NativeFailHandle(NativeHandle handle);

int main() {
    NativeHandle handle = NativeHandleOpenId(9);
    NativeFailHandle(handle);
    return 0;
}
