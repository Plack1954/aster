private extern NativeHandle NativeHandleOpenId(long id);
private extern void NativeFailHandle(NativeHandle handle);

int main()
{
    try
    {
        NativeHandle handle = NativeHandleOpenId(9);
        NativeFailHandle(handle);
        Console.WriteLine("unreachable");
    }
    catch (Exception error)
    {
        Console.WriteLine(error.Message);
    }

    Console.WriteLine("native-recovered");
    return 0;
}
