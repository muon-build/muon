" SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
" SPDX-License-Identifier: GPL-3.0-only

" Author: lattis
" Description: Ale fixer muon fmt for meson files
"
function! FormatMeson(buffer) abort
  return {
  \   'command': 'muon fmt -'
  \}
endfunction

execute ale#fix#registry#Add('muon-fmt', 'FormatMeson', ['meson'], 'muon fmt for meson')

" usage:
" source /path/to/muon/contrib/muon_fmt.vim
" let g:ale_fixers = {
" \   'meson': ['muon-fmt'],
" }
