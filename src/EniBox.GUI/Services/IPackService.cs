using System;
using System.Threading;
using System.Threading.Tasks;
using EniBox.GUI.Models;

namespace EniBox.GUI.Services
{
    public interface IPackService
    {
        Task<PackResult> PackAsync(
            PackConfiguration config,
            IProgress<PackProgress>? progressCallback,
            CancellationToken cancellationToken);
    }
}
