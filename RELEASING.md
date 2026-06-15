# How to release a new version of ngPostEx

## Files to update

When bumping the version (e.g., from 5.3 to 5.4), update these files:

| File | What to change |
|------|----------------|
| `src/ngPost.pri` | `DEFINES += "APP_VERSION=\"5.4\""` |
| `README.md` | `# ngPostEx v5.4` (title) |
| `README_FR.md` | `# ngPostEx v5.4` (title) |
| `release_notes.txt` | Add new section at the top (see template below) |

## Release notes template

Add this at the top of `release_notes.txt`, before the previous release:

```
####################################################
###       Release: ngPostEx v5.4                 ###
###       date:    YYYY/MM/DD                    ###
####################################################

- Description of change 1
- Fix: description of bug fix (#issue_number)
- Feature: description of new feature
```

## Creating the release

```bash
# 1. Commit all changes
git add -A
git commit -m "Release ngPostEx v5.4"

# 2. Create and push the tag (this triggers the CI build)
git tag v5.4
git push origin master
git push origin v5.4
```

The GitHub Actions workflow will automatically:
- Build GUI and CMD for Linux x86_64, Linux ARM64, Windows x64
- Publish a Docker image to `ghcr.io/bakasurarce/ngpostex:5.4`
- Create a GitHub Release with all artifacts
- macOS builds are added to the release when the runner becomes available

## Version format

- Use semantic-ish versioning: `MAJOR.MINOR`
- Major: fork baseline (5 = fork of ngPost 4.16)
- Minor: increment for each release with fixes/features

## Notes

- The version in `ngPost.pri` is a C macro interpreted as a double, so `5.4` works but `5.4.1` won't. Use only `MAJOR.MINOR`.
- The Docker image tags are: `latest`, `5.4`, `5` (auto-generated from the git tag).
- macOS runs independently and appends to the release — don't wait for it to finish before announcing.
