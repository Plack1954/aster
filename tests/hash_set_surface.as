using System.Collections.Generic;

int main()
{
    HashSet<string> names = new();
    if (names.Count != 0) { return 1; }
    if (names.EnsureCapacity(10) < 10 || names.Capacity < 10) { return 2; }

    if (!names.Add("Aster")) { return 3; }
    if (!names.Add("Pear")) { return 4; }
    if (names.Add("Aster")) { return 5; }
    if (names.Count != 2) { return 6; }
    if (!names.Contains("Aster") || names.Contains("Missing")) { return 7; }

    HashSet<string> copied = names;
    if (!names.Remove("Aster") || names.Remove("Aster")) { return 8; }
    if (names.Contains("Aster") || !copied.Contains("Aster")) { return 9; }
    names.TrimExcess();
    if (names.Capacity != names.Count) { return 10; }
    names.TrimExcess(8);
    if (names.Capacity != 8 || names.Count != 1) { return 11; }

    HashSet<int> many = new();
    for (int i = 0; i < 512; i++)
    {
        if (!many.Add(i)) { return 12; }
    }
    for (int i = 0; i < 512; i++)
    {
        if (!many.Contains(i) || many.Add(i)) { return 13; }
    }
    for (int i = 0; i < 512; i += 3)
    {
        if (!many.Remove(i)) { return 14; }
    }
    for (int i = 0; i < 512; i++)
    {
        if (many.Contains(i) == (i % 3 == 0)) { return 15; }
    }
    many.TrimExcess();
    if (many.Capacity != many.Count) { return 16; }
    many.Clear();
    if (many.Count != 0 || !many.Add(42) || !many.Contains(42)) { return 17; }

    names.Clear();
    copied.Clear();
    return 0;
}
