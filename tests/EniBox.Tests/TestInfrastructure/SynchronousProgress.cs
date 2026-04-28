using System;

namespace EniBox.Tests.TestInfrastructure;

internal sealed class SynchronousProgress<T> : IProgress<T>
{
    private readonly Action<T> _callback;

    public SynchronousProgress(Action<T> callback)
    {
        _callback = callback ?? throw new ArgumentNullException(nameof(callback));
    }

    public void Report(T value) => _callback(value);
}
