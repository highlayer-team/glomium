

(async () => {
    const duktape = require('./build/Release/duktape_bindings.node');

    const ctx = duktape.createContext({
        gasLimit: 1600000,
        memCostPerByte: 10
    });
    duktape.setGlobal(ctx, "test", { get68: () => { console.log("called get68"); return 68 } }
    )
    const result = await duktape.evalString(ctx, `("test").length`);

    console.log("Result:", result); 
    console.log("test")

})()
