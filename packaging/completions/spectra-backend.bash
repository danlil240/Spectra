# Bash completion for spectra-backend
# Install: cp spectra-backend.bash /etc/bash_completion.d/spectra-backend
#   or:    cp spectra-backend.bash ~/.local/share/bash-completion/completions/spectra-backend

_spectra_backend() {
    local cur prev
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    case "$prev" in
        --socket)
            # Complete file paths for socket
            COMPREPLY=( $(compgen -f -- "$cur") )
            return 0
            ;;
    esac

    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "--socket --version -v --help -h" -- "$cur") )
        return 0
    fi
}

complete -F _spectra_backend spectra-backend
