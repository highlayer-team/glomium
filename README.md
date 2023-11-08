# Glomium

Glomium is a powerful JavaScript module that serves as the core for [Glome](https://github.com/rareweave/glome), a smart contract engine optimized for creating performant, scalable, and secure smart contracts on Arweave.
By providing JavaScript bindings to the Duktape JS engine, Glomium enables the embedding and secure execution of JavaScript code within a Node.js environment (eval() but isolated), with custom resource constraints such as gas limits and memory costs.

## Installation

To install Glomium to your Node.js project, just pull it off npm:

```bash
npm install glomium
```

Cloud-built binaries will be automatically pulled, no need to have node-gyp installed.

## Usage

Below is a simple example on how to use Glomium in your projects:

```js

const Glomium = require('glomium');

// Initialize a new Glomium instance with optional configuration
const glomium = new Glomium({
  gas: {
    limit: 100000,            // Set the maximum amount of gas allowed for the execution context
    memoryByteCost: 1         // Define the gas cost per byte of memory used
  }
});

// Set a global variable within the Duktape context
glomium.set('hello', 'world');

// Retrieve the value of a global variable from the Duktape context
const world = glomium.get('hello');

// Run JavaScript code in the Duktape context
const result = glomium.run(`
  function greet(name) {
    return 'Hello, ' + name + '!';
  }
  greet(hello);
`);

console.log(result); // Should output 'Hello, world!'
```

## API

### `new Glomium(config)`

Creates a new instance of Glomium with optional configuration.

- **Parameters**
  - `config` _(Object)_: The configuration object for the Glomium instance.
    - `gas` _(Object)_: Contains the gas configuration for the execution context.
      - `limit` _(number)_: The maximum amount of gas the execution context is allowed to use (default: `100000`).
      - `memoryByteCost` _(number)_: The cost of gas per byte of memory used by the context (default: `1`).

### `glomium.set(name, value)`

Sets a global variable within the Duktape execution context.

- **Parameters**
  - `name` _(string)_: The name of the global variable to set.
  - `value` _(any)_: The value to set for the global variable.

### `glomium.get(name)`

Retrieves the value of a global variable from the Duktape execution context.

- **Parameters**
  - `name` _(string)_: The name of the global variable to get.

### `glomium.run(code)`

Executes a string of JavaScript code within the Duktape execution context and returns the result.

- **Parameters**
  - `code` _(string)_: The JavaScript code to execute.

## Building 
You can build Glomium from source by executing following commands:

```bash
git clone https://github.com/rareweave/glomium
cd glomium
git submodule update --init
cd duktape
python2 tools/configure.py --output-directory src-new --source-directory src-input --config-metadata config --option-file config/sandbox_config.yaml
cd ..
node-gyp rebuild
```

This will produce required `build` directory.

## Contributions

Contributions to Glomium are welcomed as it is a foundational component of the Glome smart contract engine. Feel free to fork, submit pull requests, or raise issues.

## License

Glomium is distributed under the MIT license.