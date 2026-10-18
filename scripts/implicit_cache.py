# Cache SCons header scan results between builds; cuts no-op builds roughly in half.
# Header edits are still detected; only adding a new header that shadows an
# existing one on the include path is missed (run `pio run -t clean` then).
Import("env")
env.SetOption("implicit_cache", 1)
