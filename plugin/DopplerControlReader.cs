using System;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Threading;

namespace SDRSharp.AstroSeriesBridge
{
    internal sealed class DopplerControlReader : IDisposable
    {
        public const string MappingName = "Local\\ASRTU_DOPPLER_CONTROL_V1";
        private const uint Magic = 0x504f4441U;
        private MemoryMappedFile _mapping;
        private MemoryMappedViewAccessor _view;

        public bool TryRead(out long targetHz, out long correctionHz)
        {
            targetHz = 0;
            correctionHz = 0;
            try
            {
                if (_view == null)
                {
                    _mapping = MemoryMappedFile.OpenExisting(
                        MappingName, MemoryMappedFileRights.Read);
                    _view = _mapping.CreateViewAccessor(0, 64,
                        MemoryMappedFileAccess.Read);
                }
                int sequence = _view.ReadInt32(36);
                Thread.MemoryBarrier();
                if ((sequence & 1) != 0)
                    return false;
                if (_view.ReadUInt32(0) != Magic || _view.ReadUInt32(4) != 1 ||
                    _view.ReadInt32(32) == 0)
                    return false;
                targetHz = _view.ReadInt64(8);
                correctionHz = _view.ReadInt64(16);
                long timestamp = _view.ReadInt64(24);
                Thread.MemoryBarrier();
                if (sequence != _view.ReadInt32(36))
                    return false;
                long age = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - timestamp;
                if (timestamp <= 0 || age < 0 || age > 3000)
                {
                    CloseMapping();
                    return false;
                }
                return targetHz > 0;
            }
            catch (FileNotFoundException)
            {
                CloseMapping();
                return false;
            }
            catch
            {
                CloseMapping();
                return false;
            }
        }

        private void CloseMapping()
        {
            if (_view != null)
                _view.Dispose();
            if (_mapping != null)
                _mapping.Dispose();
            _view = null;
            _mapping = null;
        }

        public void Dispose()
        {
            CloseMapping();
        }
    }
}
