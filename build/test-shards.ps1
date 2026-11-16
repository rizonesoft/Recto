# The x64 test suite in parallel GoogleTest shards within the memory bound (D00 T04 section 11).
# build/build.ps1 dot-sources this file. A full run (no -TestFilter) starts N shard processes side by side
# (GTEST_TOTAL_SHARDS=N, GTEST_SHARD_INDEX=i, the serial list excluded), then the tests in
# tests/recto-tests/serial-tests.txt in one process of their own, where a measured latency or wall-clock limit
# is proven on a host no sibling shard is loading. Every process is created suspended, assigned to one job object
# that kills on close and only then resumed, so a test's own children (Recto.exe, git, a self-launched
# recto-tests.exe) die with it at the deadline, on a failed start and when build.ps1 is interrupted or killed.
# Windows PowerShell 5.1 compatible: CI runs `powershell -File build/build.ps1`.

. (Join-Path $PSScriptRoot 'parallelism.ps1')

$script:RectoTestShardReserveMb = 2048
$script:RectoTestShardPerShardMb = 1024
# An AddressSanitizer test process commits about 4 GB (shadow memory and the quarantine): 4,006 MB peak in each of two
# x64 Debug ASan runs side by side on 2026-11-16, so an ASan shard is budgeted 4,096 MB.
$script:RectoTestShardPerAsanShardMb = 4096
$script:RectoTestShardMax = 6
$script:RectoTestTimeoutDefaultMinutes = 30.0

if (-not ('Recto.TestShards.Job' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace Recto.TestShards {
    public sealed class Shard {
        public IntPtr Process;
        public int Id;
        public string Name;
        public string LogPath;
        public string XmlPath;
        public int ExitCode = -1;
    }

    public static class Job {
        const uint CREATE_SUSPENDED = 0x00000004;
        const uint CREATE_NO_WINDOW = 0x08000000;
        const uint CREATE_UNICODE_ENVIRONMENT = 0x00000400;
        const uint EXTENDED_STARTUPINFO_PRESENT = 0x00080000;
        const int STARTF_USESTDHANDLES = 0x00000100;
        const uint GENERIC_WRITE = 0x40000000;
        const uint GENERIC_READ = 0x80000000;
        const uint FILE_SHARE_READ = 0x1;
        const uint FILE_SHARE_WRITE = 0x2;
        const uint CREATE_ALWAYS = 2;
        const uint OPEN_EXISTING = 3;
        const uint FILE_ATTRIBUTE_NORMAL = 0x80;
        const int HANDLE_FLAG_INHERIT = 0x1;
        const uint JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000;
        const int JobObjectBasicAccountingInformation = 1;
        const int JobObjectExtendedLimitInformation = 9;
        const uint WAIT_OBJECT_0 = 0;
        const uint WAIT_TIMEOUT = 0x102;
        static readonly IntPtr PROC_THREAD_ATTRIBUTE_HANDLE_LIST = (IntPtr)0x00020002;
        static readonly IntPtr INVALID_HANDLE_VALUE = (IntPtr)(-1);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct STARTUPINFO {
            public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
            public int dwX; public int dwY; public int dwXSize; public int dwYSize;
            public int dwXCountChars; public int dwYCountChars; public int dwFillAttribute; public int dwFlags;
            public short wShowWindow; public short cbReserved2; public IntPtr lpReserved2;
            public IntPtr hStdInput; public IntPtr hStdOutput; public IntPtr hStdError;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct STARTUPINFOEX { public STARTUPINFO StartupInfo; public IntPtr lpAttributeList; }

        [StructLayout(LayoutKind.Sequential)]
        struct PROCESS_INFORMATION { public IntPtr hProcess; public IntPtr hThread; public int dwProcessId; public int dwThreadId; }

        [StructLayout(LayoutKind.Sequential)]
        struct SECURITY_ATTRIBUTES { public int nLength; public IntPtr lpSecurityDescriptor; public int bInheritHandle; }

        [StructLayout(LayoutKind.Sequential)]
        struct JOBOBJECT_BASIC_LIMIT_INFORMATION {
            public long PerProcessUserTimeLimit; public long PerJobUserTimeLimit; public uint LimitFlags;
            public UIntPtr MinimumWorkingSetSize; public UIntPtr MaximumWorkingSetSize; public uint ActiveProcessLimit;
            public UIntPtr Affinity; public uint PriorityClass; public uint SchedulingClass;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct IO_COUNTERS {
            public ulong ReadOperationCount; public ulong WriteOperationCount; public ulong OtherOperationCount;
            public ulong ReadTransferCount; public ulong WriteTransferCount; public ulong OtherTransferCount;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
            public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation; public IO_COUNTERS IoInfo;
            public UIntPtr ProcessMemoryLimit; public UIntPtr JobMemoryLimit;
            public UIntPtr PeakProcessMemoryUsed; public UIntPtr PeakJobMemoryUsed;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct JOBOBJECT_BASIC_ACCOUNTING_INFORMATION {
            public long TotalUserTime; public long TotalKernelTime; public long ThisPeriodTotalUserTime;
            public long ThisPeriodTotalKernelTime; public uint TotalPageFaultCount; public uint TotalProcesses;
            public uint ActiveProcesses; public uint TotalTerminatedProcesses;
        }

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern IntPtr CreateJobObjectW(IntPtr attributes, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref JOBOBJECT_EXTENDED_LIMIT_INFORMATION info, int length);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool QueryInformationJobObject(IntPtr job, int infoClass, out JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info, int length, IntPtr returned);
        [DllImport("kernel32.dll", SetLastError = true, EntryPoint = "QueryInformationJobObject")]
        static extern bool QueryExtendedLimits(IntPtr job, int infoClass, out JOBOBJECT_EXTENDED_LIMIT_INFORMATION info, int length, IntPtr returned);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool TerminateJobObject(IntPtr job, uint exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern IntPtr CreateFileW(string name, uint access, uint share, ref SECURITY_ATTRIBUTES sa, uint disposition, uint flags, IntPtr template);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool InitializeProcThreadAttributeList(IntPtr list, int count, int flags, ref IntPtr size);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool UpdateProcThreadAttribute(IntPtr list, uint flags, IntPtr attribute, IntPtr value, IntPtr size, IntPtr previous, IntPtr returnSize);
        [DllImport("kernel32.dll")]
        static extern void DeleteProcThreadAttributeList(IntPtr list);
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern bool CreateProcessW(string application, StringBuilder commandLine, IntPtr processAttributes, IntPtr threadAttributes,
            bool inheritHandles, uint flags, string environment, string currentDirectory, ref STARTUPINFOEX startup, out PROCESS_INFORMATION info);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr GetStdHandle(int which);
        [DllImport("kernel32.dll")]
        static extern IntPtr GetCurrentProcess();
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool DuplicateHandle(IntPtr sourceProcess, IntPtr source, IntPtr targetProcess, out IntPtr target, uint access, bool inherit, uint options);
        const uint DUPLICATE_SAME_ACCESS = 0x2;

        // An inheritable copy of this process's standard output or error (-11, -12), or of NUL when it has none, so a
        // run without a log file writes where build.ps1 writes.
        static IntPtr InheritableStd(int which, ref SECURITY_ATTRIBUTES inherit) {
            IntPtr std = GetStdHandle(which);
            IntPtr copy;
            if (std != IntPtr.Zero && std != INVALID_HANDLE_VALUE &&
                DuplicateHandle(GetCurrentProcess(), std, GetCurrentProcess(), out copy, 0, true, DUPLICATE_SAME_ACCESS)) {
                return copy;
            }
            IntPtr nul = CreateFileW("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, ref inherit, OPEN_EXISTING, 0, IntPtr.Zero);
            if (nul == INVALID_HANDLE_VALUE) throw new Win32Exception(Marshal.GetLastWin32Error(), "cannot open NUL");
            return nul;
        }
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool TerminateProcess(IntPtr process, uint exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);

        // A job whose last handle closing kills every process in it: when build.ps1 exits, is interrupted or is killed,
        // the kernel closes the handle and the shards and everything they started go with it.
        public static IntPtr Create() {
            IntPtr job = CreateJobObjectW(IntPtr.Zero, null);
            if (job == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateJobObjectW failed");
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, ref info, Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)))) {
                int error = Marshal.GetLastWin32Error();
                CloseHandle(job);
                throw new Win32Exception(error, "SetInformationJobObject(KILL_ON_JOB_CLOSE) failed");
            }
            return job;
        }

        static string Quote(string argument) {
            if (argument.Length > 0 && argument.IndexOfAny(new char[] { ' ', '\t', '"' }) < 0) return argument;
            StringBuilder quoted = new StringBuilder("\"");
            int slashes = 0;
            foreach (char c in argument) {
                if (c == '\\') { slashes++; continue; }
                if (c == '"') { quoted.Append('\\', slashes * 2 + 1); quoted.Append('"'); slashes = 0; continue; }
                quoted.Append('\\', slashes); slashes = 0; quoted.Append(c);
            }
            quoted.Append('\\', slashes * 2);
            quoted.Append('"');
            return quoted.ToString();
        }

        // This process's environment with `set` applied: NAME=VALUE adds or replaces, NAME= removes. Sorted, as
        // CreateProcessW expects, and double-NUL terminated.
        static string Environment(string[] set) {
            SortedDictionary<string, string> variables = new SortedDictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (System.Collections.DictionaryEntry entry in System.Environment.GetEnvironmentVariables()) {
                variables[(string)entry.Key] = (string)entry.Value;
            }
            if (set != null) {
                foreach (string pair in set) {
                    int eq = pair.IndexOf('=');
                    if (eq <= 0) throw new ArgumentException("environment entry is not NAME=VALUE: " + pair);
                    string name = pair.Substring(0, eq);
                    string value = pair.Substring(eq + 1);
                    if (value.Length == 0) variables.Remove(name); else variables[name] = value;
                }
            }
            StringBuilder block = new StringBuilder();
            foreach (KeyValuePair<string, string> entry in variables) {
                block.Append(entry.Key).Append('=').Append(entry.Value).Append('\0');
            }
            block.Append('\0');
            return block.ToString();
        }

        // Starts `exe` suspended, with standard output and error written to `logPath` (or, when it is null or empty, to this
        // process's own standard output and error) and no other handle inherited, assigns it to `job`, and only then
        // lets it run.
        public static Shard Start(IntPtr job, string name, string exe, string[] arguments, string workingDirectory, string logPath, string[] environment) {
            StringBuilder commandLine = new StringBuilder(Quote(exe));
            if (arguments != null) foreach (string argument in arguments) commandLine.Append(' ').Append(Quote(argument));
            SECURITY_ATTRIBUTES inherit = new SECURITY_ATTRIBUTES();
            inherit.nLength = Marshal.SizeOf(typeof(SECURITY_ATTRIBUTES));
            inherit.bInheritHandle = 1;
            IntPtr log;
            IntPtr errors = IntPtr.Zero;
            if (!string.IsNullOrEmpty(logPath)) { // PowerShell passes $null to a string parameter as ""
                log = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, ref inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, IntPtr.Zero);
                if (log == INVALID_HANDLE_VALUE) throw new Win32Exception(Marshal.GetLastWin32Error(), "cannot create " + logPath);
            } else {
                log = InheritableStd(-11, ref inherit);
                try {
                    errors = InheritableStd(-12, ref inherit);
                } catch {
                    CloseHandle(log);
                    throw;
                }
            }
            IntPtr input = CreateFileW("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, ref inherit, OPEN_EXISTING, 0, IntPtr.Zero);
            if (input == INVALID_HANDLE_VALUE) {
                int error = Marshal.GetLastWin32Error();
                CloseHandle(log);
                if (errors != IntPtr.Zero) CloseHandle(errors);
                throw new Win32Exception(error, "cannot open NUL");
            }
            int handleCount = errors != IntPtr.Zero ? 3 : 2;
            IntPtr listSize = IntPtr.Zero;
            IntPtr list = IntPtr.Zero;
            IntPtr handles = Marshal.AllocHGlobal(IntPtr.Size * 3);
            try {
                Marshal.WriteIntPtr(handles, 0, log);
                Marshal.WriteIntPtr(handles, IntPtr.Size, input);
                if (errors != IntPtr.Zero) Marshal.WriteIntPtr(handles, IntPtr.Size * 2, errors);
                InitializeProcThreadAttributeList(IntPtr.Zero, 1, 0, ref listSize);
                list = Marshal.AllocHGlobal(listSize);
                if (!InitializeProcThreadAttributeList(list, 1, 0, ref listSize)) {
                    int error = Marshal.GetLastWin32Error();
                    Marshal.FreeHGlobal(list);
                    list = IntPtr.Zero;
                    throw new Win32Exception(error, "InitializeProcThreadAttributeList failed");
                }
                if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, (IntPtr)(IntPtr.Size * handleCount), IntPtr.Zero, IntPtr.Zero)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "UpdateProcThreadAttribute(HANDLE_LIST) failed");
                }
                STARTUPINFOEX startup = new STARTUPINFOEX();
                startup.StartupInfo.cb = Marshal.SizeOf(typeof(STARTUPINFOEX));
                startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
                startup.StartupInfo.hStdInput = input;
                startup.StartupInfo.hStdOutput = log;
                startup.StartupInfo.hStdError = errors != IntPtr.Zero ? errors : log;
                startup.lpAttributeList = list;
                PROCESS_INFORMATION info;
                uint flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
                if (!CreateProcessW(exe, commandLine, IntPtr.Zero, IntPtr.Zero, true, flags, Environment(environment), workingDirectory, ref startup, out info)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "cannot start " + exe);
                }
                if (!AssignProcessToJobObject(job, info.hProcess)) {
                    int error = Marshal.GetLastWin32Error();
                    TerminateProcess(info.hProcess, 1);
                    CloseHandle(info.hThread);
                    CloseHandle(info.hProcess);
                    throw new Win32Exception(error, "AssignProcessToJobObject failed for " + name);
                }
                if (ResumeThread(info.hThread) == 0xFFFFFFFF) {
                    int error = Marshal.GetLastWin32Error();
                    CloseHandle(info.hThread);
                    CloseHandle(info.hProcess);
                    throw new Win32Exception(error, "ResumeThread failed for " + name);
                }
                CloseHandle(info.hThread);
                Shard shard = new Shard();
                shard.Process = info.hProcess;
                shard.Id = info.dwProcessId;
                shard.Name = name;
                shard.LogPath = logPath;
                return shard;
            } finally {
                if (list != IntPtr.Zero) { DeleteProcThreadAttributeList(list); Marshal.FreeHGlobal(list); }
                Marshal.FreeHGlobal(handles);
                CloseHandle(log);
                if (errors != IntPtr.Zero) CloseHandle(errors);
                CloseHandle(input);
            }
        }

        // Waits until every shard has exited or `deadlineUtc` passes. True when all exited; their exit codes are read.
        public static bool WaitAll(Shard[] shards, DateTime deadlineUtc) {
            foreach (Shard shard in shards) {
                double left = (deadlineUtc - DateTime.UtcNow).TotalMilliseconds;
                uint wait = left <= 0 ? 0u : (uint)Math.Min(left, 0xFFFFFFFEu);
                if (WaitForSingleObject(shard.Process, wait) != WAIT_OBJECT_0) return false;
                uint code;
                shard.ExitCode = GetExitCodeProcess(shard.Process, out code) ? unchecked((int)code) : -1;
            }
            return true;
        }

        // The most memory any one process in the job committed, and the job as a whole, in MB.
        public static long[] PeakCommitMb(IntPtr job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
            if (!QueryExtendedLimits(job, JobObjectExtendedLimitInformation, out info, Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)), IntPtr.Zero)) {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "QueryInformationJobObject(extended limits) failed");
            }
            return new long[] { (long)(info.PeakProcessMemoryUsed.ToUInt64() / (1024 * 1024)), (long)(info.PeakJobMemoryUsed.ToUInt64() / (1024 * 1024)) };
        }

        public static bool Exited(Shard shard) {
            return WaitForSingleObject(shard.Process, 0) == WAIT_OBJECT_0;
        }

        public static int ActiveProcesses(IntPtr job) {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info;
            if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, out info, Marshal.SizeOf(typeof(JOBOBJECT_BASIC_ACCOUNTING_INFORMATION)), IntPtr.Zero)) {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "QueryInformationJobObject failed");
            }
            return (int)info.ActiveProcesses;
        }

        // Kills every process in the job and waits, up to `waitMs`, until none is left. True when the job is empty.
        public static bool Kill(IntPtr job, int waitMs) {
            TerminateJobObject(job, 124);
            DateTime until = DateTime.UtcNow.AddMilliseconds(waitMs);
            while (ActiveProcesses(job) > 0) {
                if (DateTime.UtcNow >= until) return false;
                System.Threading.Thread.Sleep(50);
            }
            return true;
        }

        public static void Close(IntPtr job, Shard[] shards) {
            if (shards != null) foreach (Shard shard in shards) if (shard != null && shard.Process != IntPtr.Zero) { CloseHandle(shard.Process); shard.Process = IntPtr.Zero; }
            if (job != IntPtr.Zero) CloseHandle(job);
        }
    }
}
'@
}

# How many shard processes a full run starts, and why. RECTO_TEST_SHARDS (1 to the core count) wins; otherwise the
# free memory read now, with section 3's formula (2,048 MB reserve) and the measured 1,024 MB per shard (a 6-shard Debug run peaked at 784 MB in one process), capped at one shard per physical core and at 6,
# 4,096 MB per shard under AddressSanitizer, and 1 when memory is short or cannot be read.
function Resolve-TestShardCount {
    param(
        [int]$Cores = [Environment]::ProcessorCount,
        [AllowNull()][AllowEmptyString()][string]$ShardsEnv = $env:RECTO_TEST_SHARDS,
        [switch]$Asan,
        [AllowNull()][object]$AvailableMb = $null,
        [int]$PhysicalCores = 0
    )
    if ($Cores -lt 1) { throw "Invalid logical processor count: $Cores." }
    if ($null -ne $ShardsEnv -and $ShardsEnv.Length -gt 0) {
        $parsed = 0
        $ok = [int]::TryParse($ShardsEnv.Trim(), [System.Globalization.NumberStyles]::None, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$parsed)
        if (-not $ok -or $parsed -lt 1 -or $parsed -gt $Cores) {
            throw "Invalid RECTO_TEST_SHARDS='$ShardsEnv': expected an integer between 1 and $Cores (logical processors on this machine)."
        }
        return [pscustomobject]@{ Count = $parsed; FreeMb = $null; Reason = "RECTO_TEST_SHARDS=$parsed" }
    }
    $freeMb = $null
    try {
        if ($null -ne $AvailableMb) {
            $value = 0.0
            if (-not (Test-PositiveFiniteNumber -Value $AvailableMb -Parsed ([ref]$value))) { throw "unusable value '$AvailableMb'" }
            $freeMb = [int][Math]::Floor($value)
        } else {
            $freeMb = Read-AvailableMemoryMb
        }
    } catch {
        return [pscustomobject]@{ Count = 1; FreeMb = $null; Reason = "free memory unreadable ($($_.Exception.Message)), one process" }
    }
    # One shard per physical core: the tests start worker pools of their own (RenderPool, the tile scheduler), and a
    # shard on every logical processor starves them (on a 4-vCPU CI runner, 4 shards ran three tests past the 20 s
    # Debug budget). A count Windows cannot report is taken as half the logical processors.
    if ($PhysicalCores -lt 1) { $PhysicalCores = Get-PhysicalCoreCount -Cores $Cores }
    $PhysicalCores = [Math]::Max(1, [Math]::Min($PhysicalCores, $Cores))
    $freeText = $freeMb.ToString('N0', [System.Globalization.CultureInfo]::InvariantCulture)
    $perShard = if ($Asan) { $script:RectoTestShardPerAsanShardMb } else { $script:RectoTestShardPerShardMb }
    $perText = $perShard.ToString('N0', [System.Globalization.CultureInfo]::InvariantCulture)
    $fit = [int][Math]::Floor(($freeMb - $script:RectoTestShardReserveMb) / [double]$perShard)
    $count = [Math]::Min([Math]::Min($PhysicalCores, $script:RectoTestShardMax), [Math]::Max(1, $fit))
    $kind = if ($Asan) { 'AddressSanitizer shard' } else { 'shard' }
    $why = "free $freeText MB, $PhysicalCores physical cores ($Cores logical): 2,048 MB reserve + $perText MB per $kind fits $([Math]::Max(0, $fit)), one shard per physical core, at most $($script:RectoTestShardMax)"
    if ($fit -lt 1) { $why += ', too little for a second process' }
    return [pscustomobject]@{ Count = $count; FreeMb = $freeMb; Reason = $why }
}

# The machine's physical cores (Win32_Processor.NumberOfCores over every socket), or half the logical processors when
# WMI cannot say.
function Get-PhysicalCoreCount([int]$Cores = [Environment]::ProcessorCount) {
    try {
        $sum = (Get-CimInstance -ClassName Win32_Processor -ErrorAction Stop | Measure-Object -Property NumberOfCores -Sum).Sum
        if ($null -ne $sum -and [int]$sum -ge 1) { return [int]$sum }
    } catch {
        Write-Verbose "Win32_Processor unreadable: $($_.Exception.Message)"
    }
    return [Math]::Max(1, [int][Math]::Floor($Cores / 2))
}

# The whole test run's deadline in minutes: RECTO_TEST_TIMEOUT_MINUTES (a positive number, decimals allowed) or 30.
function Resolve-TestTimeoutMinutes {
    param([AllowNull()][AllowEmptyString()][string]$TimeoutEnv = $env:RECTO_TEST_TIMEOUT_MINUTES)
    if ($null -eq $TimeoutEnv -or $TimeoutEnv.Trim().Length -eq 0) { return $script:RectoTestTimeoutDefaultMinutes }
    $value = 0.0
    if (-not [double]::TryParse($TimeoutEnv.Trim(), [System.Globalization.NumberStyles]::AllowDecimalPoint, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$value) -or
        [double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -le 0.0) {
        throw "Invalid RECTO_TEST_TIMEOUT_MINUTES='$TimeoutEnv': expected a positive number of minutes (decimals allowed)."
    }
    return $value
}

# The gtest filter patterns in tests/recto-tests/serial-tests.txt: one per line, '#' starts a comment. A pattern
# holding ':' or a leading '-' would change the filter it is joined into, so it is refused.
function Read-SerialTestPatterns([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "serial test list not found: $Path" }
    $patterns = @()
    $lineNo = 0
    foreach ($line in [System.IO.File]::ReadAllLines($Path)) {
        $lineNo++
        $text = $line
        $hash = $text.IndexOf('#')
        if ($hash -ge 0) { $text = $text.Substring(0, $hash) }
        $text = $text.Trim()
        if ($text.Length -eq 0) { continue }
        if ($text.Contains(':') -or $text.StartsWith('-') -or $text -match '\s') {
            throw "${Path}:${lineNo}: '$text' is not one gtest filter pattern (no ':', leading '-' or space)"
        }
        $patterns += $text
    }
    return , $patterns
}

# Starts each of `Specs` (Name, Arguments, LogPath, Environment as NAME=VALUE / NAME= entries) inside `Job`, in order.
# When one cannot start, every process already started is killed with what it started, and the error names it.
function Start-RectoTestProcessSet {
    param(
        [IntPtr]$Job,
        [string]$Exe,
        [string[]]$PrefixArguments = @(),
        [string]$WorkingDirectory,
        [object[]]$Specs
    )
    $started = @()
    foreach ($spec in $Specs) {
        try {
            $started += [Recto.TestShards.Job]::Start($Job, $spec.Name, $Exe, [string[]]@($PrefixArguments + $spec.Arguments), $WorkingDirectory, $spec.LogPath, [string[]]$spec.Environment)
        } catch {
            $message = $_.Exception.Message
            if ($_.Exception.InnerException) { $message = $_.Exception.InnerException.Message }
            $clean = [Recto.TestShards.Job]::Kill($Job, 10000)
            [Recto.TestShards.Job]::Close([IntPtr]::Zero, [Recto.TestShards.Shard[]]$started)
            $note = if ($clean) { 'every process already started was stopped' } else { 'a process already started was still alive 10 s after it was killed' }
            throw "recto-tests: could not start $($spec.Name) ($message); $note"
        }
    }
    return , ([Recto.TestShards.Shard[]]$started)
}

# Waits for `Shards` until `DeadlineUtc`, in half-second slices so Ctrl+C reaches build.ps1 between them. On the
# deadline every process in `Job` is killed and the error says so.
function Wait-RectoTestProcessSet {
    param([IntPtr]$Job, [Recto.TestShards.Shard[]]$Shards, [DateTime]$DeadlineUtc, [double]$TimeoutMinutes)
    while ($true) {
        $slice = [DateTime]::UtcNow.AddMilliseconds(500)
        if ($slice -gt $DeadlineUtc) { $slice = $DeadlineUtc }
        if ([Recto.TestShards.Job]::WaitAll($Shards, $slice)) { return }
        if ([DateTime]::UtcNow -ge $DeadlineUtc) {
            $running = @($Shards | Where-Object { -not [Recto.TestShards.Job]::Exited($_) } | ForEach-Object { $_.Name })
            $clean = [Recto.TestShards.Job]::Kill($Job, 10000)
            $note = if ($clean) { 'killed with every process it started' } else { 'killed, but a process was still alive 10 s later' }
            $minutes = $TimeoutMinutes.ToString([System.Globalization.CultureInfo]::InvariantCulture)
            throw "recto-tests passed its deadline of $minutes minute(s) (RECTO_TEST_TIMEOUT_MINUTES): $($running -join ', ') still running; $note"
        }
    }
}

# The block of each failed test from a shard's console log ([ RUN ] to [ FAILED ], at most 200 lines each) and the
# log's last 30 lines, so a failure is read without opening the file.
function Write-RectoTestShardFailure([Recto.TestShards.Shard]$Shard) {
    Write-Host "=== $($Shard.Name) failed (exit $($Shard.ExitCode)); log: $($Shard.LogPath)"
    if (-not (Test-Path -LiteralPath $Shard.LogPath)) { Write-Host '  (no log written)'; return }
    $lines = [System.IO.File]::ReadAllLines($Shard.LogPath)
    $block = $null
    foreach ($line in $lines) {
        if ($line.StartsWith('[ RUN      ] ')) { $block = New-Object System.Collections.Generic.List[string] }
        if ($null -ne $block) {
            if ($block.Count -lt 200) { $block.Add($line) }
            if ($line.StartsWith('[       OK ] ') -or $line.StartsWith('[  SKIPPED ] ')) { $block = $null }
            elseif ($line.StartsWith('[  FAILED  ] ')) { $block | ForEach-Object { Write-Host "  $_" }; $block = $null }
        }
    }
    Write-Host '  --- last 30 lines'
    $lines | Select-Object -Last 30 | ForEach-Object { Write-Host "  $_" }
}

# Runs the full suite as `Shards` processes side by side (the serial list excluded), then the serial list in one
# process, inside one kill-on-close job and under one deadline. Writes test-results-x64-<Tag>-shard<i>.xml,
# -serial.xml and test-x64-<Tag>-shard<i>.log / -serial.log into `XmlDir`, and the test list into
# test-list-x64-<Tag>.txt. Returns the XML paths. Throws, with each failed process's log printed, when a process fails,
# leaves no XML report, cannot start or passes the deadline.
function Invoke-RectoTestShards {
    param(
        [string]$Exe,
        [string[]]$PrefixArguments = @(),
        [string]$WorkingDirectory,
        [string]$XmlDir,
        [string]$Tag,
        [int]$Shards,
        [string[]]$SerialPatterns = @(),
        [double]$TimeoutMinutes = $script:RectoTestTimeoutDefaultMinutes
    )
    if ($Shards -lt 2) { throw "Invoke-RectoTestShards needs at least 2 shards (got $Shards); one process is the single run" }
    $unshard = @('GTEST_TOTAL_SHARDS=', 'GTEST_SHARD_INDEX=', 'GTEST_SHARD_STATUS_FILE=')
    $deadline = [DateTime]::UtcNow.AddMinutes($TimeoutMinutes)
    $job = [Recto.TestShards.Job]::Create()
    $all = @()
    try {
        $listPath = Join-Path $XmlDir "test-list-x64-$Tag.txt"
        $lister = Start-RectoTestProcessSet -Job $job -Exe $Exe -PrefixArguments $PrefixArguments -WorkingDirectory $WorkingDirectory -Specs @(
            [pscustomobject]@{ Name = 'test list'; Arguments = @('--gtest_list_tests'); LogPath = $listPath; Environment = $unshard })
        $all += $lister
        Wait-RectoTestProcessSet -Job $job -Shards $lister -DeadlineUtc $deadline -TimeoutMinutes $TimeoutMinutes
        if ($lister[0].ExitCode -ne 0) { throw "recto-tests --gtest_list_tests failed (exit $($lister[0].ExitCode)); output: $listPath" }

        $exclude = if ($SerialPatterns.Count -gt 0) { @("--gtest_filter=-$($SerialPatterns -join ':')") } else { @() }
        $specs = @()
        for ($i = 0; $i -lt $Shards; $i++) {
            $xml = Join-Path $XmlDir "test-results-x64-$Tag-shard$i.xml"
            if (Test-Path -LiteralPath $xml) { Remove-Item -LiteralPath $xml -Force }
            $specs += [pscustomobject]@{
                Name = "shard $i of $Shards"; XmlPath = $xml
                Arguments = @("--gtest_output=xml:$xml") + $exclude
                LogPath = Join-Path $XmlDir "test-x64-$Tag-shard$i.log"
                Environment = @("GTEST_TOTAL_SHARDS=$Shards", "GTEST_SHARD_INDEX=$i", 'GTEST_SHARD_STATUS_FILE=')
            }
        }
        $serialXml = Join-Path $XmlDir "test-results-x64-$Tag-serial.xml"
        if (Test-Path -LiteralPath $serialXml) { Remove-Item -LiteralPath $serialXml -Force }
        $started = Start-RectoTestProcessSet -Job $job -Exe $Exe -PrefixArguments $PrefixArguments -WorkingDirectory $WorkingDirectory -Specs $specs
        for ($i = 0; $i -lt $started.Count; $i++) { $started[$i].XmlPath = $specs[$i].XmlPath }
        $all += $started
        Wait-RectoTestProcessSet -Job $job -Shards $started -DeadlineUtc $deadline -TimeoutMinutes $TimeoutMinutes
        $ran = @($started)
        if ($SerialPatterns.Count -gt 0) {
            $serial = Start-RectoTestProcessSet -Job $job -Exe $Exe -PrefixArguments $PrefixArguments -WorkingDirectory $WorkingDirectory -Specs @(
                [pscustomobject]@{
                    Name = 'serial pass'
                    Arguments = @("--gtest_output=xml:$serialXml", "--gtest_filter=$($SerialPatterns -join ':')")
                    LogPath = Join-Path $XmlDir "test-x64-$Tag-serial.log"
                    Environment = $unshard
                })
            $serial[0].XmlPath = $serialXml
            $all += $serial
            Wait-RectoTestProcessSet -Job $job -Shards $serial -DeadlineUtc $deadline -TimeoutMinutes $TimeoutMinutes
            $ran += $serial
        }
        $failed = @($ran | Where-Object { $_.ExitCode -ne 0 })
        foreach ($shard in $failed) { Write-RectoTestShardFailure $shard }
        $noXml = @($ran | Where-Object { -not (Test-Path -LiteralPath $_.XmlPath) -or (Get-Item -LiteralPath $_.XmlPath).Length -eq 0 })
        if ($failed.Count -gt 0) {
            $what = ($failed | ForEach-Object { "$($_.Name) exit $($_.ExitCode)" }) -join ', '
            throw "recto-tests failed: $what"
        }
        if ($noXml.Count -gt 0) {
            throw "recto-tests did not produce XML report: $(($noXml | ForEach-Object { "$($_.Name) ($($_.XmlPath))" }) -join ', ')"
        }
        $peak = [Recto.TestShards.Job]::PeakCommitMb($job)
        Write-Host "recto-tests peak commit: $($peak[0]) MB in one process, $($peak[1]) MB across the run"
        return [pscustomobject]@{ Xml = @($ran | ForEach-Object { $_.XmlPath }); List = $listPath; Processes = $ran; PeakProcessMb = $peak[0]; PeakJobMb = $peak[1] }
    } finally {
        # Closing the job's last handle kills whatever is still in it: nothing a test started outlives the run.
        [Recto.TestShards.Job]::Close($job, [Recto.TestShards.Shard[]]$all)
    }
}

# The single-process run (one shard, or a -TestFilter run): one process over the whole selection, its output on this
# console as it runs, inside the same kill-on-close job and under the same deadline as a sharded run. Returns the exit
# code; throws when it cannot start or passes the deadline, with everything it started killed.
function Invoke-RectoTestSingle {
    param(
        [string]$Exe,
        [string[]]$PrefixArguments = @(),
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory,
        [double]$TimeoutMinutes = $script:RectoTestTimeoutDefaultMinutes
    )
    $deadline = [DateTime]::UtcNow.AddMinutes($TimeoutMinutes)
    $job = [Recto.TestShards.Job]::Create()
    $started = $null
    try {
        $started = Start-RectoTestProcessSet -Job $job -Exe $Exe -PrefixArguments $PrefixArguments -WorkingDirectory $WorkingDirectory -Specs @(
            [pscustomobject]@{ Name = 'recto-tests'; Arguments = $Arguments; LogPath = $null; Environment = @() })
        Wait-RectoTestProcessSet -Job $job -Shards $started -DeadlineUtc $deadline -TimeoutMinutes $TimeoutMinutes
        return $started[0].ExitCode
    } finally {
        [Recto.TestShards.Job]::Close($job, [Recto.TestShards.Shard[]]$started)
    }
}
