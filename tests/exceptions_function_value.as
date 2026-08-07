private delegate int Operation();

private struct OperationHolder
{
    Operation operation;
}

private int Fail()
{
    throw new Exception("indirect failure");
}

private int Boundary(Operation operation)
{
    try
    {
        return operation();
    }
    catch (Exception error)
    {
        if (error.Message == "indirect failure")
        {
            return 42;
        }
        return 1;
    }
}

private void OperationHolder.Set(ref OperationHolder self, Operation operation)
{
    self.operation = operation;
}

private int Succeed()
{
    return 7;
}

int main()
{
    Operation operation = Fail;
    if (Boundary(operation) != 42)
    {
        return 1;
    }
    OperationHolder holder = new() { operation = Fail };
    holder.Set(Succeed);
    Operation selected = holder.operation;
    if (selected() != 7)
    {
        return 2;
    }
    return 0;
}
