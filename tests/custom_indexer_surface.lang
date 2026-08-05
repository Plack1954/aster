struct Catalog
{
    int Base;
}

private int Catalog.Item(Catalog self, int index)
{
    return self.Base + index * 2;
}

private Catalog MakeCatalog()
{
    return new() { Base = 10 };
}

int main()
{
    Catalog catalog = MakeCatalog();
    if (catalog[3] != 16) { return 1; }
    if (MakeCatalog()[5] != 20) { return 2; }
    return 0;
}
