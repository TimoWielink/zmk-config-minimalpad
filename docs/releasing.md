# Versions and releases

The firmware uses [semantic versioning](https://semver.org). Its version, such as `0.3.0`, is in the `VERSION` file at the root of this repo, the only place to change it. The firmware reports it to MinimalPad Studio for Mac, and each build on GitHub Actions is named after it:

| Build | What you download |
| --- | --- |
| A push to a branch | `minimalpad-v0.3.0+abcdef0`: the version, then the commit it was built from |
| A release tag | `minimalpad-v0.3.0`, and a GitHub Release with the `.uf2` file |

## Raising the version

After a release, raise the version in the first commit that changes the firmware. Changes to the README or docs don't count. Pick the part by the biggest change until the next release:

- **Patch** (`0.3.0` to `0.3.1`): fixes only.
- **Minor** (`0.3.1` to `0.4.0`): something new that works with your setup as it is, such as the idle fade.
- **Major** (`0.4.0` to `1.0.0`): a change that needs something redone, such as a new layer layout, pairing again, or updating MinimalPad Studio to reach the pad.

When the version changes, update the version at the top of the [README](../README.md) too.

## Releasing a version

1. In [CHANGELOG.md](../CHANGELOG.md), write what it changes under a heading with its number, such as `## 0.3.0`. This becomes the release's description, so write it for the people who will flash it.
2. Tag the commit whose `VERSION` has that version, and push the tag:

   ```sh
   git tag v0.3.0
   git push origin v0.3.0
   ```

GitHub Actions builds the firmware and publishes a release called "MinimalPad firmware v0.3.0" with one file, `minimalpad_with_studio-v0.3.0.uf2`. Its description is your changelog section, followed by how to flash it ([.github/release-notes.md](../.github/release-notes.md)).

Before building, the workflow stops if the tag and `VERSION` disagree or the changelog has nothing for that version. Then delete the tag with `git tag -d v0.3.0` and `git push origin :v0.3.0`, fix what the error says, and tag again.
