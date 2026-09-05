# Contributing

## Development build

```bash
git clone https://github.com/slightlabs/CxxScript.git
cd CxxScript
cmake -S . -B build
cmake --build build -j
cd build && ctest --output-on-failure
```

## Running a single test binary

```bash
./build/tests/test_arrays
./build/tests/test_control_flow --gtest_filter=ControlFlowTest.*
```

## Adding a new test

1. Add a `test_*.cpp` file under `tests/`.
2. Register it in `CMakeLists.txt` (add_executable, target_link_libraries, `gtest_discover_tests`,
   and add it to the `run_tests` target's `DEPENDS` list).
3. Run `ctest --output-on-failure` to confirm it's picked up.

## Documentation site

The docs live under `docs/` and are built with [MkDocs Material](https://squidfunk.github.io/mkdocs-material/).

```bash
pip install -r docs/requirements.txt
mkdocs serve   # live preview at http://127.0.0.1:8000
```

The `Deploy Docs` GitHub Actions workflow publishes `docs/` to GitHub Pages on every push to `main`
and on every version tag.

## Release process

Releases are automated:

1. Merge to `main` — CI runs the full build/test matrix and Conan package build.
2. Once CI succeeds, the `Auto Tag` workflow creates the next `vMAJOR.MINOR.PATCH` tag automatically.
3. The new tag triggers the `Release` workflow (builds artifacts, publishes a GitHub Release) and
   the `Deploy Docs` workflow (publishes the website).

No manual tagging is required for routine changes. For a major/minor bump, push an annotated tag
manually (e.g. `git tag v1.0.0 && git push origin v1.0.0`) and subsequent auto-tags will continue
from that version.
