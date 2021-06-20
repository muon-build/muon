" Author: lattis
" Description: Ale linter muon for meson files
"
function! ale_linters#meson#muon#GetExecutable(buffer) abort
	return 'muon'
endfunction

function! ale_linters#meson#muon#GetCommand(buffer) abort
	let l:executable = ale_linters#meson#muon#GetExecutable(a:buffer)

	return ale#path#BufferCdString(a:buffer)
	\   . ale#Escape(l:executable) . ' check -'
endfunction

function! ale_linters#meson#muon#Handle(buffer, lines) abort
	let l:pattern = '\v^.*:(\d+):(\d+): (.*)$'
	let l:output = []

	for l:match in ale#util#GetMatches(a:lines, l:pattern)
		call add(l:output, {
		\ 'lnum': l:match[1] + 0,
		\ 'col': l:match[2] + 0,
		\ 'type': 'W',
		\ 'text': l:match[3],
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
