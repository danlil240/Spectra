# Fish completion for spectra-backend
# Install: cp spectra-backend.fish ~/.config/fish/completions/spectra-backend.fish

complete -c spectra-backend -l socket -d 'Unix socket path to listen on' -r -F
complete -c spectra-backend -l version -s v -d 'Print version and exit'
complete -c spectra-backend -l help -s h -d 'Show usage information'
