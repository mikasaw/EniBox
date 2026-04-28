using System;
using System.Threading;
using System.Threading.Tasks;
using EniBox.GUI.Models;

namespace EniBox.GUI.Services
{
    public interface IPackService
    {
        PackResult Pack(
            PackConfiguration config,
            IProgress<PackProgress>? progressCallback,
            CancellationToken cancellationToken);

        Task<PackResult> PackAsync(
            PackConfiguration config,
            IProgress<PackProgress>? progressCallback,
            CancellationToken cancellationToken);
    }
}
