# IncludeGuardian

IncludeGuardian is a tool to improve C/C++ compilation times by highlighting which
include directives are expensive so that you can attempt to remove them by using
techniques such as forward declarations and the pimpl idiom.

See `https://includeguardian.io` for more information and instructions on how
to install and use this tool.

## Initial Setup

### Windows

  1. Install Microsoft Visual Studio 2022
  2. Install Python 3
  3. `pip install conan`
  4. Install [ninja](https://ninja-build.org/) and add to PATH
  5. Install [CMake](https://cmake.org/download/) and add to PATH
  6. Install [vcpkg](https://vcpkg.io/en/getting-started.html)
  7. `vcpkg integrate install` (you'll get a message about a `-DCMAKE_TOOLCHAIN_FILE` variable)
  8. `cmake -S src -B build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_TOOLCHAIN_FILE=C:\Program Files\vcpkg\scripts\buildsystems\vcpkg.cmake" "-DVCPKG_TARGET_TRIPLET=x64-windows-static"` (Fixing the `CMAKE_TOOLCHAIN_FILE` variable)

### Linux (Ubuntu)
If you don't have a GCC version supporting C++20 then first follow https://askubuntu.com/a/1163021
  1. `sudo add-apt-repository ppa:ubuntu-toolchain-r/test`
  2. `sudo apt-get update`
  3. `sudo apt-get install gcc-9 g++-9`
  4. `gcc-9 --version`
  5. `sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 60 --slave /usr/bin/g++ g++ /usr/bin/g++-9`
  6. `sudo apt install libtbb-dev` (may not be necessary)

Otherwise/then follow:
  1. `sudo apt update && sudo apt upgrade`
  2. `sudo apt install clang`
  3. `sudo update-alternatives --config cc` (choose clang)+ (not needed for Ubuntu 18)
  4. `sudo apt install python3`
  5. `sudo apt install python3-pip`
  6. `pip install conan`
  7. `sudo apt install cmake`
  8. `sudo apt install pkg-config`
  9. `sudo apt install curl zip unzip tar`
  10. `git clone https://github.com/Microsoft/vcpkg.git`
  11. `./vcpkg/bootstrap-vcpkg.sh`
  12. `cd vcpkg`
    * `./vcpkg install llvm[core,clang,tools,target-x86,enable-rtti]`+
    * `./vcpkg install termcolor`
    * `./vcpkg install boost`
    * `./vcpkg install gtest`
    * `./vcpkg install benchmark`
  13. `./vcpkg integrate install` (you'll get a message about a `-DCMAKE_TOOLCHAIN_FILE` variable)
  14. `cmake -S src -B release "-DCMAKE_TOOLCHAIN_FILE=[fill in]/vcpkg/scripts/buildsystems/vcpkg.cmake" "-DCMAKE_BUILD_TYPE=Release"` or `cmake -S src -B debug "-DCMAKE_TOOLCHAIN_FILE=[fill in]/vcpkg/scripts/buildsystems/vcpkg.cmake" "-DCMAKE_BUILD_TYPE=Debug`
  15. `cd` into the correct directory
  16. `make -j`

## Building

### Windows
  1. Open `out\includeguardian.sln` with Microsoft Visual Studio
  2. Ctrl + Shift + B

### Linux
  1. `make -j`
  2. `./tests`

## Debugging
  1. `cmake -S src -B build "-DCMAKE_BUILD_TYPE=Debug" "-DCMAKE_TOOLCHAIN_FILE=[fill in]/vcpkg/scripts/buildsystems/vcpkg.cmake"`
  2. `gdb -tui --args ./tests --gtest_filter=*`
  3. `set debug-file-directory .`

## Testing
  1. Run `includeguardian.exe -p path/to/db source.cpp -output=X` where `path/to/db` contains a `compile_commands.json` file containing a [compilation database](https://clang.llvm.org/docs/JSONCompilationDatabase.html), `source.cpp` is the source file you want to operate on, and `X` is:
    * `dot-graph` to create a DOT graph of include files
    * `most-expensive` to list the include directives ordered by the file size saved if they were removed individually

## Running the Website

  1. `npm install -g local-web-server`
  2. `cd website`
  3. `ws`

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

## TODO
  * For another analysis, see if we can look if a set of includes have the
    exact same set of includers, and figure out the benefit of combining these
    components.
  * For another analysis, see if there is a component that is included by
    only one other file and suggest inlining it as a private component.


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

### Deprecated/Alternative/Banned includes

For the CI, we should be able to have a ban/deprecate list of includes (such
as <intrin.h>) and recommend alternatives.

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

