Windows XP x86 Ctrl+C acceptance tests

Extract this whole directory to a writable local folder on XP.
Run run-tests.cmd, or run-tests.cmd nopause from a command prompt.
The helper starts only child editors with disposable fixture files.
28 cases cover Release/Debug standalone/DLL programs.
Check summary.txt (Failures: 0) and xp-system.txt. Individual logs stay in cases/.

This package does not itself prove XP runtime acceptance.
The host currently has no configured XP VM. Wine tests and static PE checks
are recorded separately in the repository verification report.
