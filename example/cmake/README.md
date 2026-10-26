# ASWToml CMake Example

Configure and build from the repository root:

```text
cmake -S example/cmake -B example/cmake/build
cmake --build example/cmake/build --config Release
```

The executable is written to `example/cmake/build/bin` (in a `Release` sub folder for multi-config
generators such as Visual Studio). It builds at C++20, the library's minimum; add
`-DASWTOML_CXX_STANDARD=23` (or `26`, in another build folder) to build it at a later standard. The
root project builds it too with `-DASWTOML_BUILD_EXAMPLE=ON`.

The example writes `ASWTomlExample.toml` in the `ASWTomlExample` folder of the temp folder, starting
afresh on each run, and prints each step, the messages the library reports and the file. It returns
0 when every step did what it shows. Run it with `--pause` to wait for Enter before it exits.
