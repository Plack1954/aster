using Aster.Html;
using System;

private class GreetingCard
{
    private string name;

    public GreetingCard(string value)
    {
        name = value;
    }

    public Html Render()
    {
        return <article><strong>{name}</strong></article>;
    }

    ~GreetingCard()
    {
        Console.WriteLine("component dropped");
    }
}

private Html Page()
{
    return <main><GreetingCard value="Ada" /></main>;
}

int main()
{
    Console.WriteLine(Page().ToHtmlString());
    return 0;
}
