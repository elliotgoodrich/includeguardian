# IncludeGuardian

## TODO
  * Make improvement to clang to not disable include guards if `#` is
    encountered.  Must remove a call to `MIOpt.NextToken()` inside
    `Lexer.cpp`.
    This is inline with what GCC does https://gcc.gnu.org/onlinedocs/cppinternals/Guard-Macros.html
    and will improve the performance of `boost/preprocessor`.
  * For another analysis, see if we can look if a set of includes have the
    exact same set of includers, and figure out the benefit of combining these
    components.
  * For another analysis, see if there is a component that is included by
    only one other file and suggest inlining it as a private component.

## MVP Plan

  1. [x] Implement functionality to list the most expensive includes
  2. [x] Implement functionality to list the most expensive files
  3. [ ] Add webhook that is called from changes to the remote and stores the results in S3
  4. [ ] Create a website to view these results
  5. [ ] Implement a PR check to see if we may cause a slowdown
  6. [ ] Figure out third party library dependencies

## Initial Setup

  1. Install Microsoft Visual Studio 2019
  2. Install Python 3
  3. `pip install conan`
  4. Install [ninja](https://ninja-build.org/) and add to PATH
  5. Install [CMake](https://cmake.org/download/) and add to PATH
  6. Install [vcpkg](https://vcpkg.io/en/getting-started.html)
  7. Open `cmd` as admin
  8. vcpkg
    * vcpkg install llvm[tools,target-x86]:x64-windows
    * vcpkg install termcolor:x64-windows
  9. Open CMake GUI to `src`, set build directory to be `../out` and set the `LLVM_DIR`/`Clang_DIR` `${VCPKG_DIR}/installed/x64-windows/share/clang`/`llvm`
  10. Set `ZLIB_ROOT` to the `C:/Users/Elliot/.conan/data/zlib/1.2.12/_/_/package/3fb49604f9c2f729b85ba3115852006824e72cab` (use the conan output path)
  11. vcpkg install benchmark:x64-windows
  12. Add `benchmark_DIR` as `C:\Program Files\vcpkg\installed\x64-windows\share\benchmark` (search for `benchmarkConfig.cmake`) when configuring in CMake
  13. Add `termcolor_DIR` as `C:\Program Files\vcpkg\installed\x64-windows\share\termcolor` (search for `termcolorConfig.cmake`) when configuring in CMake
  14. Select the Visual Studio 2019 generator
  15. Click generate

## Building
  1. Open `out\includeguardian.sln` with Microsoft Visual Studio
  2. Ctrl + Shift + B

## Testing
  1. Run `includeguardian.exe -p path/to/db source.cpp -output=X` where `path/to/db` contains a `compile_commands.json` file containing a [compilation database](https://clang.llvm.org/docs/JSONCompilationDatabase.html), `source.cpp` is the source file you want to operate on, and `X` is:
    * `dot-graph` to create a DOT graph of include files
    * `most-expensive` to list the include directives ordered by the file size saved if they were removed individually

## People to thank
  * https://reversed.top/2015-04-23/detecting-wrong-first-include/
    * For helping with the proper behaviour of `ExecuteAction` to avoid "PP is null" assertions

## Aim

IncludeGuardian will sort all of the include directives in your project by the
total number of bytes of code that would need to be compiled by your
application.  This will give developers a targetted list of include
directives to attempt to remove in order to reduce compilation time.

For example:

```
  foo.cpp                          bar.cpp
  +---------------------+          +--------------------+
  | #include <foo.hpp>  |          | #include <bar.hpp> |
  |                     |          | #include <foo.hpp> |
  | // other code       |          |                    |
  +---------------------+    .-----| // more code       |
              |             /      +--------------------+
              |            /                  |
  foo.hpp     v           v        bar.hpp    v
  +-----------------------+        +-----------------------+
  | #include <common.hpp> |        | #include <common.hpp> |
  | #include <iostream>   |        | #include <memory>     |
  |                       |        |                       |
  | // code               |        | // lorem ipsum        |
  +-----------------------+        +-----------------------+
              |                        |
              \     common.hpp         v
               \    +----------------------+
                '-->| #include <algorithm> |
                    | #include <iostream>  |
                    +----------------------+
```

It probably should list the following include directories in the order of:

| Include Directive                      | `<iostream>` | `<algorithm>` | `<memory>` | `foo.hpp` | `bar.hpp` | `common.hpp` |
| -------------------------------------- |:------------:|:-------------:|:----------:|:---------:|:---------:|:------------:|
| `#include <foo.hpp>` in `foo.cpp`      |      X       |       X       |            |     X     |           |      X       |
| `#include <algorithm>` in `common.hpp` |              |      2X       |            |           |           |              |
| `#include <common.hpp>` in `foo.hpp`   |              |       X       |            |           |           |      X       |
| `#include <bar.hpp>` in `bar.cpp`      |              |               |      X     |           |    X      |              |
| `#include <common.hpp>` in `bar.hpp`   |              |               |      X     |           |           |              |
| `#include <memory>` in `bar.hpp`       |              |               |      X     |           |           |              |
| `#include <foo.hpp>` in `bar.cpp`      |              |               |            |     X     |           |              |

Assuming that `<iostream>` > `<algorithm>` > `<memory>` and the non-standard
headers are small in comparison.

Some of these suggestion are probably not feasible, for example removing
`#include <foo.hpp>` from `foo.cpp`.

Additionally, IncludeGuardian will warn developers when they open a pull
request that they are adding in an include directive that is particularly
expensive.  This should be configurable by the user, for example warn if
an include directive is in the top decile of expensive directives.

Although creating the initial list may be expensive, IncludeGuard should
strive to have a quick response on new pull requests.

## Alternative Ideas

### `find_unused_components`

Find a header/source pair where the header is not included by any other
component

### Bad include macro

Check to see if its UB (i.e. leading underscore)

### Combine results

For example, some headers may only be necessary on certain platforms and we
may accidentally report that some components are unused.  In the CI, we
can combine these results to only warn for components that are unused
across multiple settings.

### Visualization

For the CI, we should be able to have a really nice graph visualization where
we can zoom in and out on our components.  Then if you click on a component
we can zoom in to see the methods etc.  Clicking on a type used in a method
signature would zoom us over to that component

### Conversion to modules

Eventually IncludeGuardian is not going to be as useful as C++ projects would
swap over to modules.  However, a lot of projects will probably be doing this
bit-by-bit.  We should be able to recommend what's the best way to tackle
this to get the most incremental benefit.

### Automatic file editing

We should have the ability to make edits directly to the file if the user
wants to try a suggestion.  For example, if we recommend removing all
includes to a particular header file, we can go through all those files
including it and remove those directives.

### Seamless PCH

We can scan the existing code and recommend 1 file to precompile (e.g. `<vector>`) based on the number of sources it is included in and the size.  Then perhaps we can inject this one file into only those sources that already include it (perhaps this will need to be maintained server-side and have a CMake module pick it up).  I don't know what the
benefit will be, but it could be a cheap way to gain 10-15% without the
risk of adding having transitive includes.  I think we can actually have several PCH files and choose one per source.

### Rebuild Warning

We can look at the history of the repository to figure out what the chances
are that a particular header file is modified and then look at how many
files would need to be rebuilt if this was changed.  This would give us a list
of header files that cause the most rebuilding when developers are working on
changes.  This would continue to be useful even if an application moved to
modules.  We should weight the number of lines changed per file as that would
be an indication on how often it was modified during development.

### Editor plugin

Create an editor plugin that will highlight include directives in different
shades dependending on the cost (most likely total file size) of that include.
It may not be entirely accurate as removing one large include may have little
effect if there are other includes in the same file that have a significant
overlap.  However, if you have one "red" (i.e. very large) include and the
others are "orange" or "green", then you can be reasonably confident that
removing this "red" include will have an improvement, if we use a logarithmic
scale for colouring.  For example, "green" files are <1K lines, orange are
1-10K lines, and red are 10-100K lines.

