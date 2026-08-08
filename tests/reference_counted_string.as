int main()
{
    string first = "Aster";
    string second = copy(first);

    Console.WriteLine(first);
    Console.WriteLine(second);

    second = "Pear";
    Console.WriteLine(first);
    Console.WriteLine(second);
    return 0;
}
