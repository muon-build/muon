#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eux

if [ ! -d ~/.ssh ]; then
	exit 0
fi

cat >> ~/.ssh/config <<EOF
Host github.com
    IdentityFile ~/.ssh/18083346-dfba-4050-bc05-413561f99228
    IdentitiesOnly yes
    StrictHostKeyChecking no
EOF

chmod 600 ~/.ssh/config

ref=""
git for-each-ref -s --format='ref=%(refname)' refs/remotes/origin | while read -r line; do
	eval "$line"
	branch="${ref##refs/remotes/origin/}"

	if tools/ci/ref_is_release_branch.sh "refs/heads/$branch"; then
		printf "+%s:refs/heads/%s " "$ref" "$branch" >> branches_to_push.txt
	fi
done

git remote add github git@github.com:muon-build/muon.git

# We don't want to use --mirror here because that implies --prune which will
# remove any branches that might have been created on the gh repo only.
# shellcheck disable=SC2046 # Intended splitting of branches
git push --force --tags github $(cat branches_to_push.txt)
