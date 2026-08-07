using System;

delegate long Reader();

private abstract class Animal
{
    public abstract long Speak();
    public abstract long Age { get; }

    public virtual long Legs()
    {
        return 4;
    }

    ~Animal()
    {
        Console.WriteLine(100);
    }
}

private class Dog : Animal
{
    public Dog()
    {
    }

    public override long Speak()
    {
        return 7;
    }

    public override long Age => 5;

    ~Dog()
    {
        Console.WriteLine(200);
    }
}

private sealed class Bird : Animal
{
    public Bird()
    {
    }

    public override long Speak()
    {
        return 2;
    }

    public override long Age => 1;

    public sealed override long Legs()
    {
        return 2;
    }
}

int main()
{
    Dog dog = new Dog();
    Animal animal = dog;
    Console.WriteLine(animal.Speak());
    Console.WriteLine(animal.Legs());
    Console.WriteLine(animal.Age);
    Reader speak = animal.Speak;
    Console.WriteLine(speak());
    delete animal;

    Bird bird = new Bird();
    Animal flying = bird;
    Console.WriteLine(flying.Speak());
    Console.WriteLine(flying.Legs());
    Console.WriteLine(flying.Age);
    delete flying;
    return 0;
}
