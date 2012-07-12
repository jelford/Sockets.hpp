# Sockets.hpp

A thin C++ wrapper around the POSIX sockets API. Support for non-blocking IO.

## Usage

Find headers under `<source_dir>/include/`.

If you have `cmake`, you can build the static library as follows:

    cmake <source_dir>
    make

Which will spit out `libSockets.a`.

If you prefer not to use `cmake`, then something like the following should
do the trick:

    g++ -I include -c lib/Socket.cpp -o socket.o
    ar rcs libSocket.a socket.o

You can generate a shared library similarly.

In general, you'll be using `Sockets.hpp` as part of another project. If you're
using `cmake` for the other project, you can just copy (or symlink, 
or use `git submodule`) the whole source tree
somewhere into your project's source tree, then in your project's CMakeLists.txt
do:

    include_directories("${PROJECT_SOURCE_DIR}/Sockets/include/") # Sockets.hpp
    add_subdirectories (Sockets)                                  # Build the library

    ...

    target_link_libraries (<your_project> Sockets)

## License

Provided as-is. You are free to copy, redistribute, use, statically link, modify,
and so on, with or without attribution. If you do find this useful, I'd like to
hear about it.

## Further Work

 * There are currently no tests at all. It would be nice to have some tests for
    this library, if it's to be relied upon.
 * There has been no trial of IPV6 or UNIX sockets at all. They might work
    straight out of the box.
 * There is no support for `recv` or `send` -- only `read` and `write`. The former
    are more powerful (and more flexible), so support should be added.
 * There is no specific support (and no testing) for non-TCP socket
    types.
