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

## Native HTML class components

A class with one explicit constructor and a public zero-argument
`Html Render()` method may be used in native HTML element position. Constructor
parameter names become checked component properties:

```aster
private class GreetingCard
{
    private string name;

    public GreetingCard(string value)
    {
        name = value;
    }

    public Html Render()
    {
        return <article>{name}</article>;
    }
}

private Html Page()
{
    return <main><GreetingCard value="Ada" /></main>;
}
```

The compiler constructs the class, calls `Render`, and deletes the temporary
instance after its owned HTML has been produced. This gives SSR class components
ordinary Aster construction and deterministic destruction without a base class,
interface, lifecycle API, or `component` keyword.

The implementation intentionally supports exactly one constructor; body
children use an `Html children` constructor parameter just like a function
component.

An interactive class component may bind its instance methods in native HTML;
handlers can remain private implementation details:

```aster
<button onclick=this.RemoveTodo>Remove</button>
```

The browser compiler emits a component marker, a constructor/drop ABI, and
owner-qualified method exports. JavaScript creates one Wasm class instance per
rendered component region, prepends that retained instance to bound method
calls, and drops it when the region disconnects from the DOM. Successive events
therefore observe the same owned fields. A Chrome trial appends two keyed rows,
removes one of the newly appended rows, and preserves a browser-edited input;
the second append proves that `nextId` and the list survived the first event.
A separate trial renders two instances of one counter class, mutates them to
independent values, removes each region, and observes a static destructor count
of exactly one per retained object.

Synchronous exceptions cross an explicit owned exception ABI rather than
leaving the generated C exception slot pending. The browser takes and frees the
message, reports the error to the host, and retains the component object. The
trial mutates an instance, throws, then successfully invokes it again before
removal; its destructor still runs exactly once. A constructor-fault trial
returns no handle, clears the owned exception, and retries construction on the
next event rather than caching partial state. Input allocations and unused
owned results follow the same event cleanup path.

Interactive constructors may now take Boolean, integer, and `string`
parameters when the class stores each parameter in a same-named, same-typed
field. SSR writes typed, escaped constructor metadata on the compiler-owned
component root. This metadata is browser-visible and must not contain secrets.
On the first event, JavaScript owns temporary string input buffers, calls the
generated constructor ABI with those values, clears any
constructor fault, and caches only a successful object. Two `SeededCounter`
regions initialized with distinct string, integer, and Boolean values prove
that Wasm starts from each region's SSR constructor state rather than a
zero-argument reconstruction.

A synchronous `void` instance handler with `aria-controls` now triggers the
generated component `Render()` ABI automatically. The browser extracts that
controlled keyed region from the fresh native HTML, validates its inferred
parts, and applies the snapshot to retained nodes. The persistent todo handlers
therefore only mutate `this.todos`; they no longer return `Rows()` or any DOM
result. Render faults leave the old DOM in place and follow host error reporting.

Handlers may still return other supported browser results when appropriate.
A component's first `List<T>` field also has an initial structured-state path
when `T` is a flat struct of Boolean, integer, and string fields represented by
its keyed native HTML. SSR emits checked field IDs and raw escaped values; before
the first handler, JavaScript clears the constructor list and rebuilds it through
generated Wasm clear/add exports. The browser fixture deliberately changes one
SSR todo from the constructor fixture and proves the Wasm list adopts that value.

Without `aria-controls`, a `void` handler now rerenders compiler-owned scalar
parts across the component root. Part matching excludes nested component and
keyed-item ownership boundaries. Structural or conditional whole-component
changes still require a bounded controlled region. Nested instances are not
replaced by parent part updates and are disposed when an ancestor disconnects,
but conditional nested mounting is not yet implemented. Async instance handlers are supported. Starting an async handler acquires a
pending-task lease on its retained component. A newer transition on any handler
in the same component makes older completion results stale. Disconnect marks the
instance unusable immediately, suppresses rendering, and defers its destructor
until all tasks have settled and their results have been dropped. A successful
`Task`/`void` completion follows the same automatic render rule as a synchronous
`void` handler; a fault reports through the host and preserves the previous DOM.
The browser runtime does not currently cancel host work, but detached work is
safely drained without a stale DOM write.

Nested state structs, multiple list fields, request-only objects, and general
post-constructor object graphs remain unsupported. These limits are explicit;
there is no lifecycle API or leaked page-global component singleton.

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
