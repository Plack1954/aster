using Aster.Html;

private class MissingRender
{
    public MissingRender()
    {
    }
}

private Html Page()
{
    return <MissingRender />;
}

int main()
{
    Page();
    return 0;
}
