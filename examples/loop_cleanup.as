private extern NativeHandle NativeHandleOpenId(long id);
private extern long NativeHandleDropLog();

int main() {
    long iteration = 1;
    while (iteration < 3) {
        NativeHandle handle = NativeHandleOpenId(iteration);
        iteration = iteration + 1;
        continue;
    }
    Console.WriteLine(NativeHandleDropLog());

    while (true) {
        NativeHandle finalHandle = NativeHandleOpenId(3);
        break;
    }
    Console.WriteLine(NativeHandleDropLog());
    return 0;
}
