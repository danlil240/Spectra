#compdef spectra-backend
# Zsh completion for spectra-backend
# Install: cp spectra-backend.zsh /usr/share/zsh/site-functions/_spectra-backend
#   or:    cp spectra-backend.zsh ~/.zsh/completions/_spectra-backend

_spectra_backend() {
    _arguments \
        '--socket[Unix socket path to listen on]:socket path:_files' \
        '(--version -v)'{--version,-v}'[Print version and exit]' \
        '(--help -h)'{--help,-h}'[Show usage information]'
}

_spectra_backend "$@"
