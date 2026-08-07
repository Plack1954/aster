using Aster.Html;

private class InteractiveCard
{
    private string value;

    public InteractiveCard(string initial)
    {
        value = initial;
    }

    public Html Change()
    {
        value = "changed";
        return this.Render();
    }

    public Html Render()
    {
        return <section><button onclick=this.Change>{value}</button></section>;
    }
}

private Html Page()
{
    return <InteractiveCard initial="initial" />;
}

int main()
{
    Page();
    return 0;
}
