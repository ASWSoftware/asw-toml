# Guarantees: errors, threads, lifetimes and saving

What ASWToml promises about failures, threads, object lifetimes, memory and stack, and the files it
saves. The header comments have the details of each class and operation.

## Problems are returned and reported, not thrown

- `Load()`, `LoadText()`, `Save()`, `SaveAs()` and every other operation that returns a
  `TASWTomlResult` never throw. A failure is returned (`IsOk()` is false, with its `Code`, a
  `Message` that is a complete sentence, and the file path, line, column, path and system error
  when they apply) and reported to the log callback as an error. An unexpected exception inside one
  (in practice `std::bad_alloc`, or one an application's converter or validator throws) is returned
  as `UnexpectedException`, with nothing changed. `TASWTomlResult` is `[[nodiscard]]`.
- A failed operation changes nothing: a load that fails keeps the document and the file path it
  had, a save that fails leaves the file, the document, the file path and `IsModified()` as they
  were, and an edit that fails leaves the document as it was. A text with an error never loads in
  part: the first error fails the load (see [Format.md](Format.md)). The one exception is a missing
  file on `Load()` (an application's first run): the document is emptied (and given the settings'
  defaults) and the path becomes the file path, so `Save()` creates the file; the result is
  `FileNotFound`, reported as a warning.
- Warnings (a backup that couldn't be made, a value `GetValue()` couldn't convert, ...) don't make
  an operation fail; they are only reported.
- The operations that have no result to carry an error may throw `std::bad_alloc`: the lookups that
  return strings, names or a copy (`GetComment()`, `GetKeyNames()`, `GetDocument()`, `ToText()`,
  ...), and `Remove()`, whose `false` means there was nothing to remove. `GetValue()` and a
  setting's `Get()` may also throw what a converter or validator throws. `TryGetValue()` returns its
  failure and reports nothing, since the caller decides what a failure means.
- An exception thrown by the log callback is dropped. A message that can't be made (out of memory)
  is dropped.

### Out of memory

The library's tests make each allocation of loading, saving, editing, applying the settings and a
setting's `Set()` fail in turn, and check that the result is `UnexpectedException` with nothing
changed, or a success that is complete. A save that fails after making its temporary file deletes
it. With MSVC's debug iterators (a Debug build's default), the standard library allocates in some
`noexcept` move constructors, so a failed allocation there ends the program (`std::terminate()`);
release builds don't.

## Thread safety

- A TOML object (`TASWBaseToml`, and a settings class derived from it) may be used from any number
  of threads at once, with no locking by the application. Each operation is complete in itself: a
  lookup sees the document before or after an edit or a load, never during one.
- Lookups share a reader/writer lock (`std::shared_mutex`), and edits take it alone. `Load()` reads
  and parses the file without that lock and then swaps the new document in, and `Save()` writes the
  file without it, so lookups and edits go on while a file is read or written. A second lock
  (`std::mutex`) keeps `Load()`, `LoadText()`, `Save()`, `SaveAs()` and `SetOptions()` from
  overlapping.
- An edit made while a save is writing the file isn't in that save, and `IsModified()` stays `true`
  after it, so the next save writes it.
- The operations give out copies (strings, names, values, `GetDocument()`'s copy of the document),
  never pointers into the document, which another thread may change.
- A setting's `Get()`, `Set()`, `Reset()` and `IsPresent()` are the owner's operations, as safe as
  them. Several calls are separate operations, though: another thread may see some of several
  `Set()`s done and not the others. There is no edit of several keys under one lock in 1.0.
- Each edit writes and parses the whole text under the exclusive lock (microseconds for a settings
  file), so the object is for settings that are read often and written now and then, not for state
  that changes many times a second (see [Settings.md](Settings.md)). libstdc++'s
  `std::shared_mutex` (GCC's standard library, on Linux and with MinGW) isn't fair to writers: with
  several threads editing back to back, one edit can wait hundreds of milliseconds, even seconds.
  libc++ and MSVC's standard library wait milliseconds at most.
- `TASWTomlDocument` on its own isn't thread-safe (it is a plain value, copyable and movable: copy
  it, or guard it).
- Two processes that use the same file aren't coordinated (there is no cross-process file lock in
  1.0): each save replaces the whole file, so the last save wins, and a load reads either the old or
  the new file, never a mix.

## Which thread runs a callback, and what it may do

- The log callback is called on the thread of the operation that reports, after the operation's
  work, with none of the object's locks held. So it may call the object, even `Load()`, `Save()` or
  `SetLogCallback()`, without a deadlock. A `Get()` or `GetValue()` that reports a value that isn't
  valid calls it on the reading thread.
- Messages are reported in the order they happened; a load reports its own warnings first, then
  what applying the settings changed.
- Any thread may replace the callback at any time; a report that started before may still go to
  the callback it replaced.
- Converters and validators run while `Load()` applies the settings, and an override of the
  protected `ReadFile()` or `WriteFile()` while `Load()` or `Save()` works, holding the object's
  file lock (but not the document's lock). They must not call the object's `Load()`, `LoadText()`,
  `Save()`, `SaveAs()` or `SetOptions()`, which would wait for that lock forever. A validator
  should only look at the value it gets.

## Lifetime

- A TOML object can't be copied or moved. Its tables and settings must not outlive it, which holds
  when they are members of the settings class; local ones are destroyed before it. A table is
  declared before its settings.
- Tables and settings are bound to the object from their constructors to their destructors. One
  made or destroyed while another thread loads waits for the load, so a load never applies a
  setting that is half made or half destroyed.
- Whatever the log callback captures must outlive the object, or the callback must be replaced
  first (e.g. `SetLogCallback(nullptr)`) before it goes.
- A `TASWTomlDocument`'s pointers (`Find()`, `GetRoot()`, and `AsString()` and the others of a
  value in it) are valid until the document is changed, parsed again or destroyed. `GetDocument()`
  returns a copy, so keep it in a variable before finding in it: `toml.GetDocument().Find("a")`
  points into a temporary that is gone at the end of the statement.

## Nesting and stack

A value nested deep enough could overflow the stack, since reading, editing, copying, comparing and
destroying a value recurse through its tables and arrays. So `Reading.MaxNestingDepth` (100 by
default, at most 128) limits it: a deeper text fails to load with `NestingTooDeep`, and so does an
edit that would nest a value deeper (see [Options.md](Options.md)). At 128, the deepest value takes
about 200 KB of stack in the measured worst case, an unoptimized MSVC build, and much less when
optimized: well under a quarter of a 1 MB thread stack (Windows' default).

## Saving and crashes

- By default a save is atomic: the text is written to a temporary file in the same folder, flushed
  to the disk, and then replaces the file (with `ReplaceFileW()`, or `MoveFileExW()` where that
  can't be used, on Windows; with `rename()` elsewhere, flushing the folder after it). So the file
  is always either the old one or the new one, whole, even when the application crashes while
  saving, and the flushes keep it so after a power failure too. A temporary file left by a failed
  save is deleted (a warning, `TempFileCleanupFailed`, when it can't be).
- The replaced file keeps its attributes and security on Windows, and its permissions (and, when
  allowed, its owner) elsewhere, where a symbolic link stays one and the file it points to is
  replaced.
- With `Saving.Backup`, the previous file is first copied to `<file>.bak`; a backup that fails is a
  warning, and the save goes on.
- With `Saving.AtomicSave` off (for a folder that can't be written to), the file is overwritten in
  place, and a crash while saving can leave it incomplete.
- A file that is loaded and saved without changes is written back byte for byte, and an edit
  changes only the text it is about (see [Format.md](Format.md)).
- The library writes only when `Save()` or `SaveAs()` is called; nothing is saved on its own, e.g.
  in a destructor.

## Independent of the locale and the platform

- Values are converted the same way in every locale: numbers never have a `,` decimal mark or digit
  separators, whatever the C or C++ locale says.
- Keys are compared byte for byte, as TOML says: `Title` isn't `title`, for every user.
- File paths are UTF-8 on every platform (`PathToUtf8()` and `Utf8ToPath()` convert a
  `std::filesystem::path`), and files are read and written in binary mode, so a file written on one
  platform reads the same on another.
- What a value converter writes always reads back as the same value (e.g. the shortest text of a
  `double` that reads back exactly), on every supported compiler.

## Codes

`ETomlCode` says what happened, in a `TASWTomlResult` and in a `TASWTomlMessage`. Its values never
change, so an application may store or compare them, and new codes are only added at the end.
`ToString(code)` gives its name.

| Value | Code | Reported as | When |
|---|---|---|---|
| 0 | `Ok` | | Success |
| 1 | `InvalidArgument` | Error | An argument that can't be used: an empty file path, a path that isn't valid or goes through a value that isn't a table, a comment where there is no line for it |
| 2 | `InvalidOptions` | Error | `TASWTomlOptions::Validate()` refused the options (see [Options.md](Options.md)) |
| 3 | `NoFilePath` | Error | `Save()` with no file path (use `SaveAs()`) |
| 4 | `FileNotFound` | Error; Warning for `Load()` | The file or a folder in its path doesn't exist |
| 5 | `AccessDenied` | Error | The operating system denied access to the file or its folder |
| 6 | `FileTooLarge` | Error | The file is larger than `Reading.MaxFileSize` |
| 7 | `OpenFailed` | Error | The file couldn't be opened, for another reason (e.g. it is a folder on Linux; Windows reports a folder as `AccessDenied`) |
| 8 | `ReadFailed` | Error | Reading the file failed |
| 9 | `UnsupportedEncoding` | Error | The text starts with a UTF-16 or UTF-32 byte order mark |
| 10 | `InvalidUtf8` | Error | The text isn't valid UTF-8 (a string, key or comment to write that isn't is `InvalidValue`) |
| 11 | `ParseFailed` | Error | The text isn't valid TOML, with the line and column of the error |
| 12 | `DuplicateKey` | Error | A key or table is defined twice in the text, or a table again as another kind of value |
| 13 | `NestingTooDeep` | Error | Values are nested deeper than `Reading.MaxNestingDepth` |
| 14 | `CreateFolderFailed` | Error | The file's folder doesn't exist and couldn't be created |
| 15 | `WriteFailed` | Error | Writing the file, or its temporary file, failed |
| 16 | `ReplaceFailed` | Error | The temporary file couldn't replace the file (e.g. a read-only file on Windows) |
| 17 | `UnexpectedException` | Error | An exception was caught, e.g. `std::bad_alloc` |
| 18 | `NotFound` | Error; not reported by `GetValue()` or `TryGetValue()` | The key or table to read, change or rename doesn't exist, or a struct's table lacks a field's key |
| 19 | `AlreadyExists` | Error | A rename to the name of a key or table that exists |
| 20 | `TypeMismatch` | Warning when a load or a read gives the default; returned by `TryGetValue()` | The value's TOML type doesn't fit the C++ type it is read as, or a table of settings is another kind of value |
| 21 | `InvalidValue` | Error; Warning when a load or a read gives the default | The value isn't one the type accepts, a validator refused it, or it can't be written |
| 22 | `ValueOutOfRange` | Error; Warning when a load or a read gives the default | A number is outside its type's range, or its setting's, or a TOML integer is beyond 64 bits |
| 23 | `BackupFailed` | Warning | The backup couldn't be made; the file was saved anyway |
| 24 | `TempFileCleanupFailed` | Warning | The temporary file of a failed save couldn't be deleted |
| 25 | `KeyAdded` | Info | A load added a setting's missing key, or a missing field, with its default value |
| 26 | `KeyRenamed` | Info | A load renamed an earlier name of a setting's missing key to it |
