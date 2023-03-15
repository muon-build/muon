" SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
" SPDX-License-Identifier: GPL-3.0-only

" Author: lattis
" Description: Ale linter muon for meson files
"
function! ale_linters#meson#muon#GetExecutable(buffer) abort
	return 'muon'
endfunction

function! ale_linters#meson#muon#GetCommand(buffer) abort
	let l:executable = ale_linters#meson#muon#GetExecutable(a:buffer)
	let l:file = resolve(expand('%:p'))
	let l:cmd = ale#Escape(l:executable)
	let l:args = 'analyze -l'


	if match(l:file, '\.meson$') != -1
		let l:args = l:args . 'i-'
	else
		let l:args = l:args . 'O' . ale#Escape(l:file)
	endif

	return l:cmd . ' ' . l:args
endfunction

function! ale_linters#meson#muon#Handle(buffer, lines) abort
	let l:pattern = '\v(^.*):(\d+):(\d+): (warning|error) (.*)$'
	let l:output = []
	let l:cur_file = resolve(expand('%:p'))

	for l:match in ale#util#GetMatches(a:lines, l:pattern)
		let l:filename = l:match[1]
		if l:filename == '-'
			let l:filename = l:cur_file
		endif

		call add(l:output, {
		\ 'filename': l:filename,
		\ 'lnum': l:match[2] + 0,
		\ 'col': l:match[3] + 0,
		\ 'type': l:match[4] == 'warning' ? 'W' : 'E',
		\ 'text': l:match[5],
		\})
	endfor

	return l:output
endfunction

call ale#linter#Define('meson', {
\   'name': 'muon',
\   'executable': function('ale_linters#meson#muon#GetExecutable'),
\   'command': function('ale_linters#meson#muon#GetCommand'),
\   'callback': 'ale_linters#meson#muon#Handle',
\   'output_stream': 'stderr',
\})

" usage:
" mkdir -p /path/to/vim/config/ale_linters/meson
" cp /path/to/muon/contrib/muon.vim
" 	/path/to/vim/config/ale_linters/meson/muon.vim
