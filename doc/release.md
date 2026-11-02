<!--
SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
SPDX-License-Identifier: GPL-3.0-only
-->

# Making a release

1. Ensure you are on the correct branch, either main or a <major>.<minor>
   tracking branch.

2. Tag the release.  The tag should be the literal version with no prefix or
   suffix (e.g. 0.7.0).

    git tag -a <major>.<minor>.<bugfix>

2. Push the tag to https://git.sr.ht/~lattis/muon.

    git push -u srht --follow-tags

3. Monitor the build.  Release artifacts should automatically build and upload
   to https://muon.build

4. Send a release email to https://lists.sr.ht/~lattis/muon.

### subject

muon <major>.<minor>.<bugfix> released

### minor body

muon <major>.<minor>.<bugfix> has been released! You can view the release notes here:

https://docs.muon.build/release_notes.html#-<major><minor><bugfix>

Stone

### bugfix body

muon <major>.<minor>.<bugfix> has been released!

Stone

5. Set the topic for #muon

    TOPIC #muon muon build system development | <major><minor><bugfix> is out! | https://muon.build

# Bugfix release branches

- Bugfix releases must be made from their <major>.<minor> branch
