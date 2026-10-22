using System;
using System.Diagnostics;
using System.IO.MemoryMappedFiles;
using System.Threading;
using SDRSharp.Radio;

namespace SDRSharp.AstroSeriesBridge
{
    internal unsafe sealed class IqBridgeProcessor : IIQProcessor, IStreamProcessor,
        IBaseProcessor, IDisposable
    {
        internal const string MappingName = @"Local\ASRTU_IQ_BRIDGE_V1";
        private const uint Magic = 0x42514941;
        private const uint Version = 1;
        private const int HeaderBytes = 64;
        private const int CapacitySamples = 262144;
        private const int ComplexBytes = 8;
        private const double OutputSampleRate = 48000.0;
        private const long MappingBytes = HeaderBytes + (long)CapacitySamples * ComplexBytes;

        private MemoryMappedFile _mapping;
        private MemoryMappedViewAccessor _view;
        private EventWaitHandle _owner;
        public string StartError { get; private set; }
        private readonly object _stateLock = new object();
        private byte* _base;
        private bool _disposed;
        private bool _enabled;
        private double _inputSampleRate = OutputSampleRate;
        private long _inputCount;
        private double _nextOutputPosition;
        private Complex _previousSample;
        private bool _havePreviousSample;

        public IqBridgeProcessor()
        {
            Enabled = true;
        }

        public bool Enabled
        {
            get { return Volatile.Read(ref _enabled); }
            set
            {
                lock (_stateLock)
                {
                    if (_disposed || value == _enabled)
                        return;
                    if (!value)
                    {
                        CloseBridge();
                        return;
                    }
                    StartError = null;
                    try
                    {
                        bool created;
                        _owner = new EventWaitHandle(false, EventResetMode.ManualReset,
                            @"Local\ASRTU_BRIDGE_OWNER_V1", out created);
                        if (!created)
                        {
                            StartError = "Another ASRTU bridge plugin is running. Stop it first.";
                            CloseBridge();
                            return;
                        }
                        _mapping = MemoryMappedFile.CreateOrOpen(
                            MappingName, MappingBytes, MemoryMappedFileAccess.ReadWrite);
                        _view = _mapping.CreateViewAccessor(0, MappingBytes,
                            MemoryMappedFileAccess.ReadWrite);
                        // Protect older bridge versions that do not own the event.
                        if (_view.ReadUInt32(0) == Magic)
                        {
                            int pid = _view.ReadInt32(56);
                            if (pid > 0)
                            {
                                bool alive = true;
                                try { using (System.Diagnostics.Process process = System.Diagnostics.Process.GetProcessById(pid))
                                    alive = !process.HasExited; }
                                catch (ArgumentException) { alive = false; }
                                if (alive)
                                {
                                    StartError = "An existing ASRTU I/Q producer is running. Close it first.";
                                    CloseBridge();
                                    return;
                                }
                            }
                        }
                        byte* pointer = null;
                        _view.SafeMemoryMappedViewHandle.AcquirePointer(ref pointer);
                        _base = pointer + _view.PointerOffset;
                        _inputCount = 0;
                        _nextOutputPosition = 0;
                        _havePreviousSample = false;
                        InitializeHeader();
                        Volatile.Write(ref _enabled, true);
                        Volatile.Write(ref *(int*)(_base + 40), 1);
                    }
                    catch (Exception error)
                    {
                        StartError = "Cannot start shared-memory bridge: " + error.Message;
                        CloseBridge();
                    }
                }
            }
        }

        // Caller holds _stateLock. Only a producer that initialized the header
        // may mark it inactive; a rejected contender must never touch it.
        private void CloseBridge()
        {
            Volatile.Write(ref _enabled, false);
            if (_base != null)
            {
                Volatile.Write(ref *(int*)(_base + 40), 0);
                Volatile.Write(ref *(int*)(_base + 56), 0);
                _view.SafeMemoryMappedViewHandle.ReleasePointer();
                _base = null;
            }
            if (_view != null) _view.Dispose();
            if (_mapping != null) _mapping.Dispose();
            if (_owner != null) _owner.Dispose();
            _view = null;
            _mapping = null;
            _owner = null;
        }

        public double SampleRate
        {
            set
            {
                if (double.IsNaN(value) || double.IsInfinity(value) ||
                    value < 8000.0 || value > 20000000.0)
                    return;
                lock (_stateLock)
                {
                    _inputSampleRate = value;
                    _inputCount = 0;
                    _nextOutputPosition = 0;
                    _havePreviousSample = false;
                    if (_base != null)
                        *(double*)(_base + 32) = value;
                }
            }
        }

        public double CurrentSampleRate
        {
            get { lock (_stateLock) { return _inputSampleRate; } }
        }
        public double BridgeSampleRate { get { return OutputSampleRate; } }
        public long SamplesWritten
        {
            get
            {
                lock (_stateLock)
                {
                    return _base == null ? 0 :
                        Volatile.Read(ref *(long*)(_base + 24));
                }
            }
        }

        public void Process(Complex* buffer, int length)
        {
            if (!Volatile.Read(ref _enabled) || Volatile.Read(ref _disposed) ||
                buffer == null || length <= 0)
                return;

            lock (_stateLock)
            {
                if (Volatile.Read(ref _disposed) || !_enabled || _base == null)
                    return;
                ulong writeIndex = (ulong)Volatile.Read(ref *(long*)(_base + 24));
                byte* data = _base + HeaderBytes;
                if (!_havePreviousSample)
                {
                    _previousSample = buffer[0];
                    _havePreviousSample = true;
                    _nextOutputPosition = 0;
                }

                long blockStart = _inputCount;
                long blockEnd = blockStart + length - 1;
                double step = _inputSampleRate / OutputSampleRate;
                while (_nextOutputPosition <= blockEnd)
                {
                    long lowerGlobal = (long)Math.Floor(_nextOutputPosition);
                    float fraction = (float)(_nextOutputPosition - lowerGlobal);
                    Complex lower;
                    Complex upper;
                    if (lowerGlobal < blockStart)
                    {
                        lower = _previousSample;
                        upper = buffer[0];
                    }
                    else
                    {
                        int lowerOffset = (int)(lowerGlobal - blockStart);
                        int upperOffset = Math.Min(lowerOffset + 1, length - 1);
                        lower = buffer[lowerOffset];
                        upper = buffer[upperOffset];
                    }

                    int ringOffset = (int)(writeIndex % CapacitySamples);
                    float* destination = (float*)(data + (long)ringOffset * ComplexBytes);
                    destination[0] = lower.Real + (upper.Real - lower.Real) * fraction;
                    destination[1] = lower.Imag + (upper.Imag - lower.Imag) * fraction;
                    writeIndex++;
                    _nextOutputPosition += step;
                }

                _previousSample = buffer[length - 1];
                _inputCount += length;
                Thread.MemoryBarrier();
                Volatile.Write(ref *(long*)(_base + 24), (long)writeIndex);
                Volatile.Write(ref *(long*)(_base + 48), Stopwatch.GetTimestamp());
            }
        }

        private void InitializeHeader()
        {
            // Preserve the cursor if a decoder retains the map across a handover.
            Volatile.Write(ref *(int*)(_base + 40), 0);
            *(uint*)(_base + 0) = Magic;
            *(uint*)(_base + 4) = Version;
            *(double*)(_base + 8) = OutputSampleRate;
            *(uint*)(_base + 16) = CapacitySamples;
            *(uint*)(_base + 20) = ComplexBytes;
            *(double*)(_base + 32) = _inputSampleRate;
            *(int*)(_base + 40) = _enabled ? 1 : 0;
            *(long*)(_base + 48) = 0;
            *(int*)(_base + 56) = System.Diagnostics.Process.GetCurrentProcess().Id;
            Thread.MemoryBarrier();
        }

        public void Dispose()
        {
            lock (_stateLock)
            {
                if (Volatile.Read(ref _disposed))
                    return;
                Volatile.Write(ref _disposed, true);
                CloseBridge();
            }
        }
    }
}
