# `panoptes`

`panoptes` is a tool to generate [`compile_commands.json`][compdb]
from exotic build systems like [hadrian][hadrian]. It's particularly
useful if you want to hack the C codebase in the ghc runtime system
and you want accurate IDE hints from a language server.

## Quick start

First, compile `panoptes.cpp` to a shared library:

```sh
$ c++ -O3 -fPIC -shared panoptes.cpp -o panoptes.so
```

Then, when you kick off a hadrian build in a ghc tree, add `LD_PRELOAD` like this:

```sh
$ LD_PRELOAD=/workspace/panoptes/panoptes.so hadrian/build --flavour=quick -j
```

After the build finishes, `panoptes.so` appends all subprocess
creation events to `events.jsonl` in the ghc tree's top-level
directory (override with the `EVENTS_PATH` environment variable).

Now, call `panoptes.py` to generate a `compile_commands.json` file
from `events.jsonl` file:

```sh
$ /workspace/panoptes/panoptes.py -i events.jsonl -o compile_commands.json
$ /workspace/panoptes/panoptes.py # input/output filenames are optional and default to above
```

Now, if you hack on the ghc rts, you'll notice much fewer errors and
more accurate IDE hints, since the language server can parse the
correct compilation flags from `compile_commands.json`.

`panoptes` can gracefully handle existing
`events.jsonl`/`compile_commands.json` and interrupted partial builds.
There's no need to clean up these files, though you may want to add
them to your global `.gitignore` for convenience.

## How does it work

`panoptes.cpp` uses the `LD_PRELOAD` mechanism to override syscalls
related to subprocess creation, like `execve` and `posix_spawn`. It
obtains an exclusive filelock on `events.jsonl`, then appends a line
of JSON object that records the arguments and working directory.

`panoptes.py` constructs a `compile_commands.json` from
`events.jsonl`. In the case of hadrian, the same C source in the ghc
rts is compiled multiple times for different flavours (e.g. threaded,
profiling, debug), each flavour is compiled with a different
combination of CPP flags. `panoptes` will try to pick a coherent set
of build flags that increases coverage as much as possible, to make
the language server recognize code paths that are only compiled for
certain flavours.

Both `panoptes.cpp` & `panoptes.py` are short and should be easily
customizable if you want to adapt it to work with other exotic build
systems.

## Comparisons against [`bear`][bear]

- `bear` supports Linux/macOS. `panoptes` only supports Linux.
- [`bear-intercept`][bear-intercept] logs richer info, and supports
  both preload/wrapper mode. `panoptes` only supports the preload
  mode, and only logs subprocess creations, without environment
  variables, timestamps or subprocess terminations.
- `panoptes` has limited support for automatically parsing response
  files passed to compilers and expanding the correct arguments into
  `compile_commands.json`.
- `panoptes` work with hadrian.
- `panoptes` is a small & self-contained codebase, it only uses system
  C++ compiler and Python3.10+

## Why this name?

Me:

> TerrorJack wrote a small tool named "panoptes" for intercepting
> subprocess creations in Linux. Please guess why he picked this name.

GPT-4:

> Without specific information about why TerrorJack chose the name
> "Panoptes" for this tool, I can only speculate based on what I know
> about the word and its context.
>
> Panoptes is a figure from Greek mythology. Argus Panoptes was a
> giant with a hundred eyes, and his name is often translated as
> "all-seeing." As such, the name has become a symbol for surveillance
> or vigilance.
>
> Considering this, and given that the tool is used for intercepting
> subprocess creations in Linux, the name "Panoptes" might be quite
> fitting. Just like the all-seeing Argus, the tool could be considered
> "all-seeing" within its domain, able to observe and monitor the
> subprocesses that are being created within the system.

[bear]: https://github.com/rizsotto/Bear
[bear-intercept]: https://github.com/rizsotto/Bear/blob/master/source/intercept/man/bear-intercept.1.md
[compdb]: https://clang.llvm.org/docs/JSONCompilationDatabase.html
[hadrian]: https://gitlab.haskell.org/ghc/ghc/-/tree/master/hadrian
