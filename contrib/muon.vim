" Author: lattis
" Description: Ale linter muon for meson files
"
function! ale_linters#meson#muon#GetExecutable(buffer) abort
	return 'muon'
endfunction

function! ale_linters#meson#muon#GetCommand(buffer) abort
	let l:executable = ale_linters#meson#muon#GetExecutable(a:buffer)

	return ale#Escape(l:executable) . ' analyze -l'
endfunction

function! ale_linters#meson#muon#Handle(buffer, lines) abort
	let l:pattern = '\v(^.*):(\d+):(\d+): (.*)$'
	let l:output = []

	for l:match in ale#util#GetMatches(a:lines, l:pattern)
		call add(l:output, {
		\ 'filename': l:match[1],
		\ 'lnum': l:match[2] + 0,
		\ 'col': l:match[3] + 0,
		\ 'type': 'W',
		\ 'text': l:match[4],
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
