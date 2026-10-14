#pragma once

// The package manager: eskiu.toml (the manifest), eskiu.lock (the resolved
// dependencies), git fetching into $ESKIU_HOME/pkgs, and the `new`, `add`, `build`,
// `run`, `test` and `fetch` commands. See pkg.cpp and docs/lang/build.md.

#include <string>

// Handle a package command (`eskiuc new|add|build|fetch|test ...`, and `eskiuc run`
// inside a package when no .esk script is named). Returns true and sets `rc` when
// argv was a package command; false leaves argv to the compiler's own options.
bool pkgCommand(int argc, char** argv, int& rc);

// Fill `packageRoots` (parser.h) for a compile whose first input is `input`: when an
// eskiu.toml is in its directory or above, the dependencies of that package's
// eskiu.lock. Nothing is fetched here.
void pkgLoadRoots(const std::string& input);
