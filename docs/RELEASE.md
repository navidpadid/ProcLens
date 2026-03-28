# Release Process

This document describes how to create new releases of the kernel module project.

## Overview

The project uses an automated release workflow that supports semantic versioning with three types of version bumps:

- **Major** (x.0.0): Breaking changes or major new features
- **Minor** (1.x.0): New features, backward compatible
- **Patch** (1.3.x): Bug fixes and minor improvements

## Release Methods

### Method 1: Pull Request Labels (Recommended)

When merging a pull request to `main`, add one of these labels to trigger an automatic release:

- `release:major` - Bumps major version (e.g., 1.3.0 → 2.0.0)
- `release:minor` - Bumps minor version (e.g., 1.3.0 → 1.4.0)
- `release:patch` - Bumps patch version (e.g., 1.3.0 → 1.3.1)

Use **only one** of the release labels above per PR. If more than one is present, the workflow will fail.

To explicitly skip releasing on a merged PR, add:
- `not-a-release` - Prevents release creation, even if a release label is present

The workflow will automatically:
1. Determine the new version number
2. Generate release notes from PR metadata (or manual dispatch context)
3. Build binaries for `amd64` and `arm64`
4. Bundle artifacts and checksum into a release archive
5. Create a git tag
6. Create a GitHub release with release notes and packaged binaries

### Method 2: Manual Workflow Dispatch

You can manually trigger a release from the GitHub Actions tab:

1. Go to **Actions** → **Release** workflow
2. Click **Run workflow**
3. Select the branch (usually `main`)
4. Choose the version bump type (major/minor/patch)
5. Click **Run workflow**

## What Happens During a Release

1. **Version Calculation**: The workflow reads the latest git tag and calculates the new version based on the bump type
2. **Release Notes Generation**:
	- For PR releases, notes include a `Changelog` section with PR title/body
	- Notes include version/type metadata
	- Notes include a `Binary package includes` section summarizing packaged contents and quick-start commands
3. **Binary Builds**: Builds `elf_det.ko` and `proc_elf_ctrl` for both `amd64` and `arm64`
4. **Packaging**: Produces a bundled archive named `elf_det-binaries-vX.Y.Z-linux-6.8.0.tar.gz` plus a `.sha256` checksum file
5. **Git Tag Creation**: Creates an annotated git tag (e.g., `v1.4.0`) if it does not already exist
6. **GitHub Release**: Creates a release on GitHub using the generated release notes and uploads the archive + checksum

> Note: The workflow does **not** update `README.md` and does **not** create a version commit.

## Version Numbering

The project follows [Semantic Versioning 2.0.0](https://semver.org/):

```
MAJOR.MINOR.PATCH

1.4.2
│ │ └─ Patch: Bug fixes, typos, documentation updates
│ └─── Minor: New features, backward compatible changes
└───── Major: Breaking changes, major rewrites
```

### Guidelines

**Use MAJOR version when:**
- Changing kernel module API
- Removing features or fields from /proc/elf_det/
- Incompatible changes to output format

**Use MINOR version when:**
- Adding new information fields to the output
- Adding new helper functions (backward compatible)
- Adding new documentation or test infrastructure

**Use PATCH version when:**
- Fixing bugs in existing functionality
- Updating documentation
- Code formatting or refactoring
- Dependency updates

## Current Version

The current version can be found by:

```bash
# Check the latest git tag
git describe --tags --abbrev=0
```

## Example Workflows

### Example 1: Bug Fix Release

```bash
# Create a PR with bug fixes
git checkout -b fix/heap-calculation
# ... make changes ...
git commit -m "fix: correct heap calculation for edge cases"
git push origin fix/heap-calculation

# Create PR and add label: release:patch
# When merged, automatically releases v1.3.1
```

### Example 2: New Feature Release

```bash
# Create a PR with new feature
git checkout -b feature/add-thread-info
# ... implement feature ...
git commit -m "feat: add thread count to process info"
git push origin feature/add-thread-info

# Create PR and add label: release:minor
# When merged, automatically releases v1.4.0
```

### Example 3: Breaking Change Release

```bash
# Create a PR with breaking changes
git checkout -b refactor/new-output-format
# ... make breaking changes ...
git commit -m "refactor!: change output format to JSON"
git push origin refactor/new-output-format

# Create PR and add label: release:major
# When merged, automatically releases v2.0.0
```

## Troubleshooting

### Release didn't trigger

**Check:**
- PR was merged to `main` branch
- PR has one of the release labels (`release:major`, `release:minor`, `release:patch`)
- GitHub Actions are enabled in repository settings

### Version number is wrong

The workflow uses `git describe --tags --abbrev=0` to find the latest version. If this fails:

```bash
# Check existing tags
git tag -l

# Manually create a version tag if needed
git tag -a v1.3.0 -m "Current version"
git push origin v1.3.0
```

### Permission errors

The workflow needs `contents: write` permissions. Check:
- Repository settings → Actions → General → Workflow permissions
- Ensure "Read and write permissions" is enabled

## Manual Release (Without Workflow)

If you need to create a release manually:

```bash
# Create and push tag
git tag -a v1.4.0 -m "Release v1.4.0"
git push origin main
git push origin v1.4.0

# Build and package binaries (matching workflow behavior)
make clean && make all

# Create GitHub release manually through web interface
# Upload bundled archive and checksum
```

## See Also

- [Semantic Versioning Specification](https://semver.org/)
- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Conventional Commits](https://www.conventionalcommits.org/) (recommended for commit messages)
