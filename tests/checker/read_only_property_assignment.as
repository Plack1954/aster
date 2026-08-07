private class Person
{
    public string Name { get; }

    public Person(string name)
    {
        Name = name;
    }
}

int main()
{
    Person person = new Person("Ada");
    person.Name = "Grace";
    delete person;
    return 0;
}
