#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

set -eu

export RCLONE_CONFIG_R2_TYPE=s3 RCLONE_CONFIG_R2_PROVIDER=Cloudflare
export RCLONE_CONFIG_R2_ACCESS_KEY_ID=$R2_ACCESS_KEY_ID
export RCLONE_CONFIG_R2_SECRET_ACCESS_KEY=$R2_SECRET_ACCESS_KEY
export RCLONE_CONFIG_R2_ENDPOINT=https://$R2_ACCOUNT_ID.r2.cloudflarestorage.com
export RCLONE_CONFIG_R2_NO_CHECK_BUCKET=true

set -x

cache_control="Cache-Control: public, max-age=300"
bucket="r2:$R2_BUCKET"

version=$1
shift

for path; do
	path=${path%/}
	if [ -d "$path" ]; then
		op=sync
	else
		op=copyto
	fi

	for dest in "$version" edge; do
		rclone "$op" "$path" "$bucket/$dest/${path##*/}" --header-upload "$cache_control"
	done
done
