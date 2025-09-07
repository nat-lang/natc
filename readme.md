## natc

This is the compiler for nat. For the language manual see [natlang.online](https://natlang.online).

### What is nat?

Nat is a language designed for modeling natural language semantics. It's a programming language with a variety of features aimed at linguists and philosophers who practice formal semantics. These include:

- Twin imperative and functional paradigms.
- A dynamic, incrementally adoptable, and extensible suite of type systems. Types are values.
- Pattern matching.
- Sundry formalism from the semanticist's everyday toolbox: set comprehensions, first class trees, quantifiers, infixable functions, and more.
- A core library of data structures and utilities for building grammars.
- Tight integration with LaTeX.
- Runtime access to the AST.

### Development

The compiler is built on top of Robert Nystrom's `clox` compiler. Much of the foundation remains the same, and a good introduction to the basic design and implementation of the compiler is chapter three of Nystrom's book [Crafting Interpreters](https://craftinginterpreters.com/contents.html).

#### Getting started
Clone the repo, install `make`, and then:

```bash
cd natc && make
```

This will compile the interpreter, place the binary in the `build` folder, and install a symlink to the binary in your local `~/.bin`.  You'll then be able to execute e.g. `file.nat` with:

```bash
nat file.nat
```

#### Running the tests

The primary tests are the integration tests under [test/integration](test/integration). The integration tests are also the de facto language definition, and a good resource for gaining a sense of the language.

To run the test suite:

```bash
make tests
```

This should produce output like the following:

```
integration ok (2.291882s)
regression  ok (1.9999999999994e-06s)
trip        ok (1.9e-05s)
```

#### Compiling to wasm

To prepare nat for use on [natlang.online](https://natlang.online) (or your local instance of [the site](https://github.com/nat-lang/www)) compile to webassembly with [emscripten](https://emscripten.org/docs/compiling/Building-Projects.html). To download `emscripten`, follow the instructions [here](https://emscripten.org/docs/getting_started/downloads.html). With emscripten installed, you should be able to wrap makefile commands with `emmake`. Run the following:

```bash
emmake make build-wasm
```

This will translate the compiler to webassembly and place a javascript entrypoint at [wasm/lib/nat.js](wasm/lib/nat.js). You'll then need to compile the client. Make sure you have node and npm installed (instructions [here](https://nodejs.org/en/download/)). Then:

```bash
cd wasm && npm install && npm run build
```

This will compile the typescript interface at [wasm/src/index.ts](wasm/src/index.ts) to javascript. Now you'll be able to expose the client with `npm link` and use it for development on any local npm project with:

```bash
npm link @nat-lang/nat
```

#### Pull requests, continuous integration, and publishing

The primary public outlet for nat is its [npm package](https://www.npmjs.com/package/@nat-lang/nat), which bundles the webassembly and typescript client. To publish changes to nat, you should open a pull request into the `main` branch. Minimally, the PR must

1. include an integration test, if applicable;
2. pass the test suite, which is automated via the github action at [.github/workflows/ci.yml](.github/workflows/ci.yml);
3. increment the semver version in the [package.json](wasm/package.json).

If steps (1) - (3) are successfull, the PR can merge to `main`. To publish the changes, open a new PR from `main` into the `release` branch. Merging into `release` will initiate an automated publication.

#### Editor support

The vscode extension at [https://github.com/nat-lang/natvs](https://github.com/nat-lang/natvs) provides syntax highlighting for nat files. Clone the repo and install the extension with:

```bash
npm run dev
```