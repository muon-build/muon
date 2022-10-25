#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eux

cat >> ~/.ssh/config <<EOF
Host github.com
    IdentityFile ~/.ssh/18083346-dfba-4050-bc05-413561f99228
    IdentitiesOnly yes
    StrictHostKeyChecking no
EOF

chmod 600 ~/.ssh/config

git remote add github git@github.com:annacrombie/muon.git
git push --force --all --follow-tags github
