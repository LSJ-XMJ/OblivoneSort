# OblivoneSort 2.0.0 release checklist

## Code and package

```text
[ ] working tree clean
[ ] version header = 2.0.0
[ ] CMake project = 2.0.0
[ ] ctest 7/7
[ ] install succeeds
[ ] installed headers contain serial + simple parallel only
[ ] installed targets: oblivone::oblivone / oblivone::parallel
[ ] advanced header and target absent from install
[ ] consumer links oblivone::parallel and reports 2.0.0
```

## CI

```text
[ ] GitHub Actions build-and-test green on Windows/Linux/macOS
[ ] TSan workflow green
[ ] TSAN_OPTIONS halt_on_error=1
```

## Documentation

```text
[ ] README release banner
[ ] CHANGELOG v2.0.0
[ ] docs/2.0.0/testing.md
[ ] docs/2.0.0/advanced-foundation.md
[ ] .zenodo.json version/date
```

## Repository-owner actions

These are not performed by the coding agent:

```text
[ ] merge/release branch decision
[ ] git tag v2.0.0
[ ] push tag
[ ] GitHub Release v2.0.0
[ ] Zenodo archive
[ ] copy new DOI into CITATION.cff
[ ] add DOI badge back to README if desired
```

## Package boundary

Official public API:

```text
oblivone::sort
oblivone::sort_by
oblivone::thread_pool
oblivone::parallel_sort
oblivone::parallel_sort_by
```

Experimental, not installed:

```text
oblivone::advanced::*
```

Do not claim the advanced scheduler as part of 2.0.0's public API.
