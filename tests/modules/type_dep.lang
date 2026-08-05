namespace Types.Dep;

public struct Resource {
    long id;
}

~Resource() {
    Console.WriteLine(self.id);
}

public Resource MakeResource(long id) {
    return Resource { id: id };
}

public void ConsumeResource(Resource value) {
    Console.WriteLine(value.id);
}
