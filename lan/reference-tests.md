# Local reference fixtures for native tests

The archive keeps the engine tests and generated synthetic assets. A small set of parser and layout tests also compares against game data supplied by the operator. Those inputs live beside the test fixtures in a local validation checkout and are ignored by Git.

For the full Warcraft III and shared-engine suite, use the local reference files listed in `reference-test-paths.json` from an earlier lawful checkout. Copy them into the same paths under `engine/`. For a new setup, the `-3.0` FDF files and the skin file come from the modern Classic SD data. The `-classic` FDF files come from the classic game archives. The panel fixture is the MDLX header and SEQS chunk from the original panel model. Keep these inputs in your local checkout.

Then run:

```sh
make -C engine HOMEBREW_PREFIX="$WC3_SDL_PREFIX" TEST_JOBS=4 -j4 test
```

The native binary and WebAssembly build can be built without these test inputs. The room service, transport, browser launcher and release staging tests use their own fixtures.
