# /etc/bash.bashrc - AxsOS etkileşimli bash ayarları
[ -z "$PS1" ] && return
[ -n "$AXSOS_BASHRC" ] && return
AXSOS_BASHRC=1

# --- geçmiş ---
HISTFILE=~/.bash_history
HISTSIZE=5000
HISTFILESIZE=20000
HISTCONTROL=ignoreboth:erasedups
shopt -s histappend checkwinsize globstar autocd cdspell dirspell extglob 2>/dev/null
PROMPT_COMMAND='history -a'

# --- istem: kullanıcı@makine:dizin (git dalı yok; son komut hata verdiyse kırmızı kod) ---
__axs_ps1() {
    local e=$?
    local c='\[\e[1;35m\]' g='\[\e[1;32m\]' b='\[\e[1;34m\]' r='\[\e[1;31m\]' z='\[\e[0m\]'
    PS1="${c}\u${z}@${g}\h${z}:${b}\w${z}"
    [ $e -ne 0 ] && PS1+=" ${r}[$e]${z}"
    PS1+='\$ '
}
PROMPT_COMMAND="__axs_ps1; $PROMPT_COMMAND"

# --- takma adlar ---
alias ls='ls --color=auto'
alias ll='ls -lh'
alias la='ls -lha'
alias l='ls -CF'
alias ..='cd ..'
alias ...='cd ../..'
alias df='df -h'
alias du='du -h'
alias free='free -m'
alias guncelle='axpkg update'
alias kur='axpkg install'
alias kaldir='axpkg remove'

# --- klavye: ↑/↓ yazılan başlangıca göre geçmişte arar ---
bind '"\e[A": history-search-backward' 2>/dev/null
bind '"\e[B": history-search-forward' 2>/dev/null
bind 'set show-all-if-ambiguous on' 2>/dev/null
bind 'set completion-ignore-case on' 2>/dev/null
bind 'set colored-stats on' 2>/dev/null

# --- tamamlama ---
complete -d cd pushd rmdir
complete -c command type which man sudo time
complete -u su
complete -A variable export unset
complete -A signal trap
_axpkg_complete() {
    local cur=${COMP_WORDS[COMP_CWORD]}
    if [ "$COMP_CWORD" -eq 1 ]; then
        COMPREPLY=($(compgen -W "install remove list available search update info files owner create" -- "$cur"))
        return
    fi
    case "${COMP_WORDS[1]}" in
        install|info)
            COMPREPLY=($(compgen -W "$(axpkg names 2>/dev/null)" -- "$cur"))
            [ ${#COMPREPLY[@]} -eq 0 ] && COMPREPLY=($(compgen -f -- "$cur")) ;;
        remove|files)
            COMPREPLY=($(compgen -W "$(ls /var/lib/axpkg/db 2>/dev/null)" -- "$cur")) ;;
        *) COMPREPLY=($(compgen -f -- "$cur")) ;;
    esac
}
complete -F _axpkg_complete axpkg kur kaldir

# --- bilinmeyen komut: depoda varsa hangi paketin kurulacağını söyle ---
command_not_found_handle() {
    local p
    p=$(axpkg provides "$1" 2>/dev/null)
    if [ -n "$p" ]; then
        printf 'bash: %s: komut bulunamadı. Kurmak için:\n  axpkg install %s\n' "$1" "$p" >&2
    else
        printf 'bash: %s: komut bulunamadı\n' "$1" >&2
    fi
    return 127
}
