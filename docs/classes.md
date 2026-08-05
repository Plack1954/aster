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
class Node
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
class Counter
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
class reference, does nothing for `null`, invokes the declared destructor once,
drops owned value fields in reverse declaration order, and frees the object.

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

struct aster_type_N {
    int64_t f0;
    aster_type_N *f1;
};
```

The numeric type name is compiler-generated. Forward declarations allow self
references and mutual class references. Empty classes receive one private byte
in generated C because C17 has no standard empty-structure representation.

The bytecode backend also recognizes class references as pointer-like runtime
values. It does not automatically retain or release them.

## Properties

Classes and structs may declare C#-style properties. A property is part of the
callable member surface; it is not a public field with decorative syntax.
Reads invoke its getter and writes invoke its setter in both backends.

```csharp
class Counter
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

Static properties with explicit accessor bodies are supported through the
ordinary static-member lowering. Static automatic properties are not yet
supported because Aster does not yet have static field storage.

## Deliberate current boundary

The following features remain intentionally unavailable:

- inheritance, virtual dispatch, and interfaces.

Generic classes currently receive a targeted diagnostic.

No garbage collector or automatic reference-counting scheme is implied by this
design. `new` creates a stable heap object, assignment aliases it, and `delete`
destroys and frees it under programmer control.
