# Stability

From `1.0.0`, ssg follows [SemVer](https://semver.org). Changing anything on this page needs a
major version; new keys, flags, commands, hooks and manifest fields may appear in any minor
version. Pre-releases are `1.0.0-alpha.N`, `-beta.N` and `-rc.N`; releases are tagged `vX.Y.Z`.
`ssg --version` prints the version and, when it is known, the git commit.

## Frozen

| Surface | Promise | Documented in |
| --- | --- | --- |
| `site.toml` | Existing keys keep their meaning; new keys are additive; unknown keys still warn | [configuration.md](configuration.md), `ssg(5)` |
| Template syntax | `{{key}}`, `extend`, `include`, `if`/`else`/`end`, `\{{` escapes and the built-in keys | [templates.md](templates.md) |
| CLI | Subcommand names, flags and exit codes (0 ok, 1 error, 2 usage, 3 warnings with `--fail-on-warning`); `ssg <dir>` stays an alias for `ssg build <dir>` throughout 1.x | [cli.md](cli.md), `ssg(1)` |
| `--json` output | `"schema":1`; fields are only added | [cli.md](cli.md#json-output) |
| Output layout | `foo/index.{html,md,txt}` and the URL rules | [getting-started.md](getting-started.md#where-pages-go) |
| Manifest | `manifest.json` carries a schema version (1); changes are additive; readers ignore unknown fields | [configuration.md](configuration.md#manifestjson) |
| Plugin protocol | Protocol number 1; hooks and message fields are additive only; ssg refuses a plugin that needs a newer protocol | [plugins.md](plugins.md) |
| Markdown | CommonMark 0.31.2 plus the documented extensions; a new extension needs an opt-in key | [markdown.md](markdown.md) |
| Sanitizing | Raw HTML passes only through the allow-list; the list may grow, but a tag or attribute that is escaped or dropped today is never silently let through by default if it can run script | [markdown.md](markdown.md) |

## Not frozen

- The wording of errors, warnings and notes (script against exit codes and `--json`).
- The exact wrapping of `.txt` output.
- Performance, and which files are rewritten by `--incremental`.
- The generated deploy files for `--target` (`worker.js`, `vercel.json`, nginx snippet): they
  follow the hosts, and what they do (serve the format by `Accept`, send the headers) is the
  promise, not their text.
- Anything marked as an example, such as the contents of `example/` and the first-party plugin
  outputs (their config keys are additive, like any `site.toml` key).

## Cutting a release

Source builds report `-dev`. A pre-release or release is built with the matching suffix, so
`ssg --version` agrees with the changelog and the tag:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSSG_WERROR=ON -DSSG_VERSION_SUFFIX=-rc.1
cmake --build build -j && ctest --test-dir build --output-on-failure && sh tests/run.sh build/ssg
build/ssg --version    # ssg 1.0.0-rc.1 (<commit>)
```

Run the same tests in a `-DSSG_SANITIZE=ON` Debug build before tagging. Use `-DSSG_VERSION_SUFFIX=`
(empty) for the final release.
