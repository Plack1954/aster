# Classes

Aster classes are reference types with C#-shaped syntax and deliberately
manual lifetime management. They are not value-like structs, garbage-collected
objects, reference-counted handles, or Rust-style ownership wrappers.

The implementation establishes the following foundations:

- `class` declarations are a distinct source and checked-type category;
- class values have pointer identity, while their fields occupy a separate
  heap-object layout;
- constructors allocate initialized objects through `new`;
- `delete` explicitly runs destruction and releases object storage.

For example:

```csharp
private class Node
{
    long Value;
    Node Next;
}
```

`Node` is a pointer-sized reference. `Node Next` therefore stores another
pointer and does not recursively embed a complete `Node`. Self-referential and
mutually referential object graphs are consequently representable without a
special indirection type.

## Value and identity rules

Structs and classes intentionally mean different things:

| Declaration | Storage | Assignment | Identity |
| --- | --- | --- | --- |
| `struct` | Inline value | Copies the value according to its copy policy | No stable object identity |
| `class` | Reference to a separately allocated object | Copies the reference | Stable object address |

At the typed-IR boundary, a class reference has pointer size and alignment,
trivial copy policy, and trivial drop policy. Its object body separately records
field offsets, object size, and object alignment. Copying a class variable does
not clone the object and does not retain it through hidden reference counting.

Class references may contain `null`. Dereferencing a field through `null` and
deleting `null` trap deterministically in both backends. Flow-sensitive
nullable-reference warnings remain a future feature.

## Construction and deletion

Constructors use C# syntax:

```csharp
private class Counter
{
    private long Value;

    public Counter(long value)
    {
        Value = value;
    }

    public long Increment()
    {
        Value = Value + 1;
        return Value;
    }

    ~Counter()
    {
        Value = 0;
    }
}

Counter counter = new Counter(40);
Counter alias = counter;
long result = alias.Increment();
delete counter;
```

Construction initializes every declared field before allocating the finished
object. Assignment to `alias` copies only the reference. `delete` accepts a
private class reference, does nothing for `null`, dynamically selects the destructor
for the allocation's actual class, drops owned value fields in reverse
declaration order, and frees the object. A derived destructor automatically
continues through its base destructor chain. This remains deterministic manual
destruction; there is no tracing collector or hidden retain/release operation.

As in C and C++, aliases are not tracked. Accessing an alias after its object
has been deleted, or deleting the same allocation twice, is programmer error.
Aster does not insert hidden reference counting to make those operations safe.

Class fields and methods default to private. `public` exposes them outside the
declaring class. Private access is accepted only from methods, constructors, or
the destructor belonging to that class.

## Generated C representation

The C17 backend emits a forward-declared object structure and uses a pointer for
every value of that class type. Conceptually, the `Node` declaration above is
represented as:

```c
typedef struct aster_type_N aster_type_N;

private struct aster_type_N {
    uint32_t _type_id;
    int64_t f0;
    aster_type_N *f1;
};
```

The numeric type name and runtime type tag are compiler-generated. Forward
declarations allow self references and mutual class references. The tag drives
virtual calls, virtual delegate binding, and deletion through a base-class
reference. It is ordinary ahead-of-time metadata, not a garbage-collector
header.

The bytecode backend also recognizes class references as pointer-like runtime
values. It does not automatically retain or release them.

## Properties

Classes and structs may declare C#-style properties. A property is part of the
callable member surface; it is not a public field with decorative syntax.
Reads invoke its getter and writes invoke its setter in both backends.

```csharp
private class Counter
{
    private long Changes;

    public long Value { get; private set; }
    public string Name { get; }
    public long Doubled => Value * 2;

    public long Limited
    {
        get { return Value; }
        set
        {
            Value = value < 0 ? 0 : value;
            Changes += 1;
        }
    }

    public Counter(long initial, string name)
    {
        Value = initial;
        Name = name;
        Changes = 0;
    }
}
```

The supported forms are:

- expression-bodied read-only properties using `=>`;
- custom `get` and `set` accessor bodies;
- automatic `get;`, `set;`, and `get; private set;` accessors;
- getter-only automatic properties assignable by their declaring constructor;
- accessor-specific `public` or `private` visibility;
- ordinary and compound property assignment.

Inside a setter, `value` is the implicit parameter containing the assigned
value. An automatic property receives compiler-private backing storage. That
storage participates in constructor definite-assignment analysis and in the
same deterministic reverse-order field cleanup as an explicitly declared
field. It cannot be named directly in Aster source.

Automatic properties deliberately do not change the manual class lifetime
model. Their backing fields live inside the object allocated by `new` and are
cleaned only when that object is explicitly deleted. Copying a property value
uses its declared type's normal copy policy.

## Static members

Classes and structs support C#-style static fields, properties, and methods:

```csharp
private class Counter
{
    private static long Value = 10;

    public static long Count { get; private set; }
    public static long Current => Value;

    public static void Add(long amount)
    {
        Value += amount;
        Count += 1;
    }
}
```

Static fields occupy module storage rather than object storage. Every instance
observes the same slot, and constructing or deleting an instance does not
initialize or destroy it. The VM creates fresh static slots for each module
run. Generated C emits ordinary translation-unit `static` objects.

Fields receive their conventional zero value unless they declare a scalar
constant initializer. Boolean, integer, floating, enum, raw-pointer, and class
reference fields are supported. Owning static strings, collections, structs,
and other cleanup-bearing values are rejected until Aster has an explicit
program/module shutdown contract; the compiler does not invent a hidden
process-lifetime owner or silently leak them.

Automatic static properties use compiler-private static backing storage.
Custom and expression-bodied static properties use ordinary static accessor
methods. Member and accessor `public`/`private` rules are identical to instance
members. Static methods may refer to members of their declaring type without
repeating the type name.

## Bound instance method delegates

An instance method on a class can be converted to an exact delegate type. The
receiver is captured by the resulting delegate, so later calls use the same
object:

```csharp
private delegate long Operation(long amount);

private class Counter
{
    private long Value;

    public long Add(long amount)
    {
        Value += amount;
        return Value;
    }
}

Counter counter = new Counter();
Operation operation = counter.Add;
long result = operation(4);
```

The target delegate type selects overloaded methods using the method
parameters after `this`. Return types, parameter types, and `ref`/`out` modes
must match exactly. Private methods may be bound only from their declaring
class. Binding through `null` traps immediately.

A bound delegate is a trivially copyable pair containing an invocation target
and a borrowed class pointer. It performs no allocation and does not retain,
reference-count, clone, or own the receiver. Consequently, deleting the class
instance invalidates every delegate bound to it; calling one afterward is the
same programmer error as dereferencing any other dangling class reference.

Binding struct instance methods is currently rejected. Supporting it requires
an explicit decision between copying the struct into delegate storage and
boxing it; Aster does neither implicitly today. Bound class methods are not
general closures and cannot capture arbitrary locals.

Virtual methods retain virtual behavior when bound. If an `Animal` reference
actually points at a `Dog`, binding `animal.Speak` captures the `Dog` override,
matching C# delegate behavior. The delegate still only borrows the receiver.

## Single inheritance and virtual dispatch

Aster supports C#-shaped single class inheritance and the `abstract`,
`virtual`, `override`, and `sealed` modifiers:

```csharp
private abstract class Animal
{
    public abstract long Speak();
    public abstract long Age { get; }

    public virtual long Legs()
    {
        return 4;
    }
}

private sealed class Dog : Animal
{
    public Dog() { }

    public override long Speak() { return 1; }
    public override long Age => 5;
}

Animal animal = new Dog();
Console.WriteLine(animal.Speak());
Console.WriteLine(animal.Legs());
delete animal;
```

An abstract class cannot be instantiated. A concrete class must implement all
inherited abstract methods and property accessors. An override must exactly
match a virtual base signature, including parameter passing modes and return
type. `sealed override` closes one virtual slot, while `sealed class` prevents
further derivation. A virtual call dispatches from the allocation's runtime
private class in both bytecode and generated C; an inherited implementation remains
the target when a derived class does not override it.

Class-reference assignment supports the conventional implicit derived-to-base
conversion. Aster does not permit an implicit base-to-derived downcast.
Overload selection remains static; only a checked virtual member invocation is
dynamically dispatched.

## Interfaces

Interfaces are nominal C#-shaped contracts. They may inherit multiple
interfaces, and a class may implement multiple interfaces in addition to its
single base class:

```csharp
private interface IValue
{
    long Value();
    long Number { get; }
}

private interface IAdvanced : IValue
{
    long Twice();
}

private class Counter : BaseCounter, IAdvanced
{
    public long Value() { return 7; }
    public long Number => 8;
    public long Twice() { return 14; }
}
```

Interface members are implicitly public and abstract. A concrete class must
provide a public instance member with the exact name, return type, parameter
types, and `ref`/`out` modes. An implementation inherited from a base class is
valid. One class member may satisfy matching slots from several interfaces;
Aster records those slots independently rather than forcing the method into a
single fake override chain.

Interface conversion is nominal. A class converts to an interface only when
its declaration or a base class names that interface, and a derived interface
converts to its inherited interfaces. Calls, properties, and bound delegates
dispatch through the allocation's runtime class in both backends. `delete`
through an interface reference likewise runs the actual class destructor and
its base chain. Interface references remain borrowed, manually managed class
pointers—there is no boxing, hidden allocation, reference counting, or garbage
collection.

Interfaces cannot be instantiated and cannot declare fields, constructors,
destructors, static members, or default method bodies. Explicit interface
implementation syntax and default interface methods are not currently part of
the language.

## Deliberate current boundary

The following features remain intentionally unavailable:

- multiple class inheritance;
- base classes with instance fields and base-constructor initializers.

The second restriction is deliberate and diagnosed. Stateless base classes
already provide polymorphic APIs, abstract contracts, virtual properties,
bound virtual delegates, and destructor chaining. Stateful inheritance will
not be accepted until Aster has a real C#-shaped `: base(...)` constructor
model and a prefix-compatible inherited-field layout; the compiler does not
silently skip base construction or flatten private state with invented rules.

Generic classes currently receive a targeted diagnostic.

No garbage collector or automatic reference-counting scheme is implied by this
design. `new` creates a stable heap object, assignment aliases it, and `delete`
destroys and frees it under programmer control.
