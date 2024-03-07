<center>

# Glomium

<img src="./glomium.svg" width="200" ></center>

Glomium is a JavaScript module that enables the embedding and secure execution of JavaScript code within a Node.js environment (eval() but isolated), with custom resource constraints such as gas limits and memory costs.

It also has ability to de-asyncify async functions to achieve full determinism of execution envinroment.

Glomium utilizes [Duktape](https://duktape.org) as its javascript engine.

Note: Only ES5 is supported, so use babel to transpile higher-versioned ecmascript to ES5 before executing it.

## Comparison table with alternatives

Glomium is worse and better in a lot of aspects compared to alternatives. 

Since we don't make money just from you using Glomium, we absolutely are not trying to get you to use Glomium.

Make sure to look at alternative solutions before choosing your engine.

Have a look at quick comparison table below:

* **Secure**: Obstructs access to unsafe nodejs capabilities
* **Memory Limits**: Possible to set memory limits / safe against heap overflow DoS attacks
* **Operation Limits**: Possible to set limit on amount of operations code can execute, providing security from infinite loops
* **Isolated**: Is garbage collection, heap, etc isolated from application
* **Speed**: Average performance on benchmarks for common usecases
* **Module Support**: Is `require` supported out of the box
* **Inspector Support**: Chrome DevTools supported
* **Full determinism**: Built with determinism in mind, has async function de-asyncifying, predictable exits due to memory/operation count limits (gas), deterministic runtime
* **ECMAScript version**: Version of ecmascript that the runtime natively supports (without babel and similar tools)
| Module                                                                       | Secure | Memory Limits | Operation Limits  | Isolated | Speed, engine | Module Support | Inspector Support | Full determinism | ECMAScript version |
| ---------------------------------------------------------------------------- | :----: | :-----------: | :--------------: | :------: | :-----------: | :------------: | :---------------: | :---------------: | :---------------: |
| [vm](https://nodejs.org/api/vm.html)                                         |        |               |                   |          |  Fast, V8+JIT  |       ✅       |        ✅         |                 | Node's ES version |
| [worker_threads](https://nodejs.org/api/worker_threads.html)                 |        |               |                   |    ✅    | Fast, V8+JIT  |       ✅       |        ✅        |               | Node's ES version |
| [vm2](https://github.com/patriksimek/vm2)                                    |        |               |                   |          | Fast, V8+JIT   |       ✅       |        ✅         |                | Node's ES version |
| [isolated-vm](https://github.com/laverdet/isolated-vm)                       |   ✅  |       ✅     | 50/50, timeouts supported |    ✅    | Fast, V8+JIT  |                |        ✅         |                | Node's ES version |
| [quickjs-emscripten](https://github.com/justjake/quickjs-emscripten)         |   ✅  |       ✅     | 50/50, timeouts supported |    ✅    | Slow, QuickJS+WASM  |        ✅        |                 | 50/50, can't be achieved without detereministic operation/memory limits  | ES2023 |
| glomium                                                                      | ✅    |      ✅      |       ✅         |    ✅ | Medium, Duktape running natively |                           |                    |  ✅         | ES5       |


## Installation

To install Glomium to your Node.js project, just pull it off npm:

```bash
npm install glomium
```

Cloud-built binaries will be automatically pulled, no need to have node-gyp installed (unless you use non-x86 architecture or non-common OS, like FreeBSD).

## Usage

Below is a simple example on how to use Glomium in your projects:

```js

const Glomium = require('glomium');

// Initialize a new Glomium instance with optional configuration
const vm = new Glomium({
  gas: {
    limit: 100000,            // Set the maximum amount of gas allowed for the execution context
    memoryByteCost: 1         // Define the gas cost per byte of memory used
  }
});
// Engine runs in separate thread, meaning each operation is async (passing call to thread and waiting for response), so let's enter async context to use await
(async()=>{
// Set a global variable within the Duktape context
vm.set('hello', 'world');

// Retrieve the value of a global variable from the Duktape context
const world = vm.get('hello');// should be "world"

// Run JavaScript code in the Duktape context
const result = await vm.run(`
  (hello+" is awesome");
`);

console.log(result); // Should output 'world is awesome'

//Glomium implements gas system for resource management, meaning amount of computation that program can do is limited by how much gas you give to it.
await vm.run(`
 while(true){}
`).catch(e=>{
  console.error(e)//Error: Out of gas
});

await vm.clear()// Clear environment, clearing all globals and any trace of something being executed on this vm. It's important to do it after fatal error has happened (such as out of gas) to avoid undefined behavior.

// Glomium also can de-asyncify functions, meaning if you pass async function to it, it will behave as sync one in glomium (awaiting). Use events or callbacks for async communication (even though it allows untrusted code to break determinism).

//Setting async function
vm.set("asyncFunction",(count)=>{
  return Promise(res=>{
    setTimeout(()=>{
      console.log(count)
    },Math.random()*100)
  })
})
//Calling it multiple times from async enviroment doesn't guarantee order of execution (if you call asyncFunction(1) and asyncFunction(2), sometimes 1 will log first, and sometimes 2 will log first)
// Await fixes this, but if untrusted code wants to achieve non-determinism, it might be a problem, so glomium executes even async functions in "sync", sequential way, meaning calling asyncFunction(1); asyncFunction(2) will always log 1 first.
await vm.run(`
for(var i=0;i<100;++i){
  asyncFunction(i)
}
`) //Should log numbers 1 to 100 one by one, without it being in shuffled order 


})();

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
- **Returns**
  Promise\<value>

### `glomium.run(code)`

Executes a string of JavaScript code within the Duktape execution context and returns the result.
Might throw on error or fatal error (out of gas is most common one).

- **Parameters**
  - `code` _(string)_: The JavaScript code to execute.
- **Returns**
  Promise\<value>

### `glomium.clear()`

Fully resets all global variables and traces of something executing in the VM, might be useful for VM reuse between contexts that shouldn't be tightly isolated (i.e same app but different task)

- **Parameters**
  Doesn't take parameters, returns undefined

### `glomium.setGas(config)`

Updates the gas configuration for the Duktape execution context associated with the current instance.

This function sets new limits and costs associated with the Duktape context's resource consumption, "gas".

- **Parameters**
  - `config` _(Object)_: An object containing the new gas configuration parameters.
    - `gasLimit` _(number)_: The maximum amount of gas that can be consumed. Often represents the upper limit of computational steps or memory usage.
    - `memoryByteCost` _(number)_: The cost per byte of allocating memory contributing to the total gas consumed.
    - `gasUsed` _(number)_: The amount of gas already consumed. This can be set to initialize or reset the consumption counter.

### `glomium.getGas()`

Returns the gas configuration.

- **Returns**
   `Promise<config>` _(Object)_: An object containing the new gas configuration parameters.
    - `gasLimit` _(number)_: The maximum amount of gas that can be consumed. Often represents the upper limit of computational steps or memory usage.
    - `memoryByteCost` _(number)_: The cost per byte of allocating memory contributing to the total gas consumed.
    - `gasUsed` _(number)_: The amount of gas already consumed. This can be set to initialize or reset the consumption counter.

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

## Support the developer

Best way to support Glomium is to do contribution!
If you want to support Glomium financially, it would be better if you support [Duktape](https://duktape.org), ensuring author of Glomium won't need to maintain its fork 😄

## Contributions

Contributions to Glomium are welcome! Feel free to do pull requests, improving performance, security audits, open issues, etc.

## License

Glomium is distributed under the MIT license.

